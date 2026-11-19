"""Address-free MCP Worker for a compatible SAN14 Host."""
from __future__ import annotations

import argparse
import asyncio
import contextvars
import errno
import os
import re
import socket
import threading

from mcp import types
from mcp.server.lowlevel import Server

from .ipc import BridgeError, DuplexRpc, WindowsPipe, authority_scope, validate_authority, _authority

SEMANTIC_VERSION = 1


def bind_loopback(port):
    # Windows may reserve the default port for Hyper-V/WinNAT even when no
    # process is listening. Explicit custom ports retain their exact meaning.
    candidates = (8765, 18765, 28765, 38765, 0) if port == 8765 else (port,)
    for index, candidate in enumerate(candidates):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            listener.bind(('127.0.0.1', candidate))
            listener.listen(128)
            listener.setblocking(False)
            return listener, candidate or listener.getsockname()[1]
        except OSError as error:
            listener.close()
            unavailable = error.errno in (errno.EACCES, errno.EADDRINUSE) or getattr(error, 'winerror', None) in (10013, 10048)
            if not unavailable or index == len(candidates)-1:
                raise


class AuthorityMiddleware:
    """Bind the listener's original owner to each ASGI call and its child tasks."""
    def __init__(self, app, authority):
        self.app, self.authority = app, validate_authority(authority, required=True)

    async def __call__(self, scope, receive, send):
        with authority_scope(self.authority):
            await self.app(scope, receive, send)


class ManagedWorker:
    def __init__(self, rpc):
        self.rpc = rpc
        self._lock = threading.RLock()
        self._accepting = True
        self._accepting_http = False
        self._active, self._writes, self._http_writes = 0, 0, 0
        self._http_server = self._http_thread = None
        self._http_error = None
        self._http_authority = None
        self._http_port = 8765
        self._http_requested_port = 8765
        self._http_stop_pending = self._worker_stop_pending = False
        self._stop_acknowledged = False
        self._backend_busy = False
        self.stopped = threading.Event()
        self.tools = {}
        from .queries import create_server
        self.queries = create_server()
        self.mcp = Server(
            'san14-companion', version=str(SEMANTIC_VERSION), title='三国志14 · 优化伴侣',
            on_list_tools=self._http_list, on_call_tool=self._http_call,
            get_tool_input_schema=lambda name: self.tools[name].input_schema if name in self.tools else None,
        )

    def _catalog(self):
        result = self.rpc.call('semantic.catalog', {'version': SEMANTIC_VERSION}, timeout=35)
        if result.get('version') != SEMANTIC_VERSION or not isinstance(result.get('tools'), list):
            raise BridgeError('incompatible', 'Host semantic protocol is incompatible')
        tools = [types.Tool.model_validate(row) for row in result['tools']]
        by_name = {tool.name: tool for tool in tools}
        if len(by_name) != len(tools):
            raise BridgeError('incompatible', 'Host declared duplicate tools')
        with self._lock:
            self.tools = by_name
        return result

    async def _http_list(self, _context, _params):
        with self._lock:
            return types.ListToolsResult(tools=list(self.tools.values()))

    async def _http_call(self, _context, params):
        return types.CallToolResult.model_validate(
            await asyncio.to_thread(self._tool_call, {
                'name': params.name, 'arguments': params.arguments or {}, 'authority': _authority.get(),
            }, from_http=True)
        )

    def _tool_call(self, params, *, from_http=False):
        if (set(params) - {'name', 'arguments', 'authority'} or
                not isinstance(params.get('name'), str) or not isinstance(params.get('arguments'), dict)):
            raise BridgeError('invalid_argument', 'tool.call requires a tool name and arguments object')
        authority = validate_authority(params.get('authority'), required=True)
        with self._lock:
            tool = self.tools.get(params['name'])
            if tool is None:
                raise BridgeError('tool_not_found', 'Tool is not in the active Host catalogue')
            writing = not (tool.annotations and tool.annotations.read_only_hint is True)
            if not self._accepting or (from_http and not self._accepting_http):
                raise BridgeError('stopping', 'MCP listener is disabled or stopping')
            self._active += 1
            self._writes += int(writing)
            self._http_writes += int(writing and from_http)
        try:
            from .queries import TOOL_NAMES, invoke
            if params['name'] in TOOL_NAMES:
                return invoke(self.queries, self.rpc, params['name'], params['arguments'], authority)
            result = self.rpc.call('semantic.invoke', {
                'version': SEMANTIC_VERSION, 'name': params['name'],
                'arguments': params['arguments'], 'authority': authority,
            }, timeout=285)
            if result.get('transport_error'):
                return {'isError': True, 'structuredContent': result,
                        'content': [{'type': 'text', 'text': result.get('reason', 'Worker transport failed')}]}
            return result
        finally:
            with self._lock:
                self._active -= 1
                self._writes -= int(writing)
                self._http_writes -= int(writing and from_http)

    def status(self):
        with self._lock:
            alive = bool(self._http_thread and self._http_thread.is_alive())
            running = bool(alive and self._http_server and self._http_server.started)
            runtime_busy = self._backend_busy
            busy = bool(self._http_writes or runtime_busy)
            state = ('busy' if self._http_stop_pending and busy else
                     'stopping' if self._http_stop_pending and alive else
                     'running' if running else 'starting' if alive else
                     'error' if self._http_error else 'stopped')
            return {'status': state, 'running': running, 'port': self._http_port, 'url': f'http://127.0.0.1:{self._http_port}/mcp' if running else None,
                    'active_writes': self._writes, 'active_http_writes': self._http_writes,
                    'runtime_busy': runtime_busy,
                    'active_calls': self._active, 'accepting_commands': self._accepting_http,
                    **({'error': self._http_error} if self._http_error else {})}

    def start_http(self, params):
        port = params.get('port', 8765)
        if (set(params) - {'host', 'port', 'authority'} or params.get('host', '127.0.0.1') != '127.0.0.1' or
                type(port) is not int or not 1 <= port <= 65535):
            raise BridgeError('invalid_argument', 'Managed MCP requires 127.0.0.1 and a port from 1 to 65535')
        authority = validate_authority(params.get('authority'), required=True)
        self._catalog()
        with self._lock:
            if not self._accepting:
                raise BridgeError('stopping', 'Worker is stopping')
            if self._http_stop_pending:
                raise BridgeError('busy', 'Previous MCP listener is still stopping')
            if self._http_thread and self._http_thread.is_alive():
                if self._http_authority != authority or self._http_requested_port != port:
                    raise BridgeError('busy', 'The existing listener has another authority or port')
                return self.status()
            import uvicorn
            listener, bound_port = bind_loopback(port)
            try:
                app = self.mcp.streamable_http_app(host='127.0.0.1', max_request_body_size=1024 * 1024)
                config = uvicorn.Config(AuthorityMiddleware(app, authority), host='127.0.0.1', port=bound_port, log_level='warning',
                                        log_config=None, use_colors=False, access_log=False, timeout_graceful_shutdown=5)
                self._http_server = uvicorn.Server(config)
            except BaseException:
                listener.close()
                raise
            self._accepting_http, self._http_error = True, None
            self._http_authority = authority
            self._http_port = bound_port
            self._http_requested_port = port
            def serve():
                try:
                    asyncio.run(self._http_server.serve(sockets=[listener]))
                except BaseException as exc:
                    with self._lock:
                        self._http_error = f'MCP listener stopped: {type(exc).__name__}'
                finally:
                    listener.close()
                    with self._lock:
                        self._accepting_http = False
                        self._http_stop_pending = False
            self._http_thread = threading.Thread(target=serve, name='san14-mcp-loopback', daemon=True)
            self._http_thread.start()
            return self.status()

    def stop_http(self):
        with self._lock:
            self._accepting_http = False
            self._http_stop_pending = bool(self._http_thread and self._http_thread.is_alive())
        self.poll()
        return self.status()

    def stop_worker(self):
        with self._lock:
            self._accepting = self._accepting_http = False
            self._worker_stop_pending = True
            self._http_stop_pending = bool(self._http_thread and self._http_thread.is_alive())
            busy = bool(self._active or self._backend_busy)
        self.poll()
        return {'status': 'busy' if busy else 'stopping', 'active_writes': self._writes,
                'active_calls': self._active}

    def poll(self):
        """Keep a stopping listener alive until its adapter work has settled."""
        with self._lock:
            stopping = self._http_stop_pending or self._worker_stop_pending
        if not stopping:
            return
        try:
            status = self.rpc.call('semantic.status', {'version': SEMANTIC_VERSION}, timeout=3)
            backend_busy = status.get('runtime_busy') is not False
        except (OSError, TimeoutError):
            backend_busy = True
        with self._lock:
            self._backend_busy = backend_busy
            if self._http_stop_pending and not self._http_writes and not backend_busy and self._http_server:
                self._http_server.should_exit = True
            if (self._worker_stop_pending and self._stop_acknowledged and not self._active and not backend_busy and
                    not (self._http_thread and self._http_thread.is_alive())):
                self.stopped.set()

    def response_sent(self, method):
        if method == 'worker.stop':
            with self._lock:
                self._stop_acknowledged = True

    def handle(self, method, params):
        if method == 'tool.call':
            if not self.tools:
                self._catalog()
            return self._tool_call(params)
        if method == 'tool.list' and (not params or params == {'native_only': True}):
            return self._catalog()
        if method == 'mcp.start':
            return self.start_http(params)
        if method == 'mcp.status' and not params:
            return self.status()
        if method == 'mcp.stop' and not params:
            return self.stop_http()
        if method == 'worker.stop' and not params:
            return self.stop_worker()
        raise BridgeError('method_not_found', 'Unknown managed worker method')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pipe', required=True)
    parser.add_argument('--token-env', required=True)
    parser.add_argument('--parent-pid', required=True, type=int)
    args = parser.parse_args(argv)
    if args.parent_pid <= 0 or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,127}', args.token_env):
        parser.error('Invalid parent PID or token environment variable')
    token = os.environ.pop(args.token_env, None)
    if not token or len(token) > 4096:
        raise BridgeError('identity', 'Missing Base worker authentication token')
    transport = WindowsPipe(args.pipe, args.parent_pid)
    ready, startup_failed = threading.Event(), threading.Event()
    holder = []
    def dispatch(method, params):
        if not ready.wait(30) or startup_failed.is_set():
            raise BridgeError('starting', 'Managed semantic worker is not ready')
        return holder[0].handle(method, params)
    rpc = DuplexRpc(transport, dispatch).start()
    worker = None
    try:
        accepted = rpc.call('hello', {'protocol': 1, 'token': token, 'worker_pid': os.getpid(),
                                      'parent_pid': args.parent_pid}, timeout=30)
        if accepted.get('protocol') != 1 or not isinstance(accepted.get('session_id'), str):
            raise BridgeError('identity', 'Invalid Host handshake')
        token = None
        worker = ManagedWorker(rpc)
        rpc.after_response = worker.response_sent
        holder.append(worker)
        ready.set()
        # The authenticated Base owns the pipe; process death closes it and
        # wakes the reader. No game process handles or polling OS reads are used.
        while not rpc.closed and not worker.stopped.wait(0.2):
            worker.poll()
    finally:
        startup_failed.set()
        ready.set()
        if worker is not None:
            if worker._http_server is not None:
                worker._http_server.should_exit = True
        rpc.close()


if __name__ == '__main__':
    main()
