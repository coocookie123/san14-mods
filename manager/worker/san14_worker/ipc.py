"""Authenticated, framed duplex IPC for the semantic worker."""
from __future__ import annotations

from contextlib import contextmanager
import contextvars
from concurrent.futures import Future, ThreadPoolExecutor
import itertools
import json
import os
import re
import struct
import threading

MAX_FRAME = 8 * 1024 * 1024
_authority = contextvars.ContextVar('san14_base_authority', default=None)


class BridgeError(OSError):
    def __init__(self, code, message):
        super().__init__(str(message))
        self.code = str(code)


def validate_authority(value, *, required=False):
    if value is None and not required:
        return None
    if not isinstance(value, str) or not re.fullmatch(r'(?:0x)?[0-9a-fA-F]{1,64}', value):
        raise BridgeError('permission', 'A Base-issued lifecycle authority is required')
    return value


@contextmanager
def authority_scope(value):
    token = _authority.set(validate_authority(value))
    try:
        yield
    finally:
        _authority.reset(token)


def _unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError('duplicate IPC field')
        value[key] = item
    return value


def encode_frame(value):
    body = json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
    if not 0 < len(body) <= MAX_FRAME:
        raise BridgeError('frame_size', 'IPC frame exceeds the size limit')
    return struct.pack('<I', len(body)) + body


def _read_exact(transport, size):
    data = bytearray()
    while len(data) < size:
        piece = transport.read(size - len(data))
        if not piece:
            raise BridgeError('disconnected', 'Base connection closed')
        data.extend(piece)
    return bytes(data)


def read_frame(transport):
    size, = struct.unpack('<I', _read_exact(transport, 4))
    if not 0 < size <= MAX_FRAME:
        raise BridgeError('frame_size', 'Invalid IPC frame size')
    try:
        value = json.loads(_read_exact(transport, size).decode('utf-8'),
                           object_pairs_hook=_unique_object,
                           parse_constant=lambda _: (_ for _ in ()).throw(ValueError('non-finite IPC number')))
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise BridgeError('invalid_frame', 'Malformed IPC JSON') from exc
    if not isinstance(value, dict):
        raise BridgeError('invalid_frame', 'IPC frame must be an object')
    return value


class DuplexRpc:
    """One reader only dispatches; handlers and nested calls use other threads."""
    def __init__(self, transport, handler=None, *, prefix='w', peer_prefix='b'):
        self.transport, self.handler = transport, handler
        self.after_response = None
        self.prefix, self.peer_prefix = prefix, peer_prefix
        self._ids = itertools.count(1)
        self._lock, self._write_lock = threading.Lock(), threading.Lock()
        self._pending, self._incoming = {}, set()
        self._closed = threading.Event()
        self._pool = ThreadPoolExecutor(max_workers=12, thread_name_prefix='san14-tools')
        self._control_pool = ThreadPoolExecutor(max_workers=3, thread_name_prefix='san14-control')
        self._reader = threading.Thread(target=self._read_loop, name='san14-base-reader', daemon=True)

    def start(self):
        self._reader.start()
        return self

    @property
    def closed(self):
        return self._closed.is_set()

    def _send(self, value):
        frame = encode_frame(value)
        with self._write_lock:
            if self.closed:
                raise BridgeError('disconnected', 'Base connection closed')
            offset = 0
            while offset < len(frame):
                written = self.transport.write(frame[offset:])
                if not written:
                    raise BridgeError('disconnected', 'Base connection closed while writing')
                offset += written

    def call(self, method, params=None, *, timeout=120):
        request_id = f'{self.prefix}:{next(self._ids)}'
        future = Future()
        with self._lock:
            if self.closed:
                raise BridgeError('disconnected', 'Base connection closed')
            self._pending[request_id] = future
        try:
            self._send({'id': request_id, 'method': method, 'params': {} if params is None else params})
            return future.result(timeout=timeout)
        finally:
            with self._lock:
                self._pending.pop(request_id, None)

    def _serve(self, value):
        try:
            if self.handler is None:
                raise BridgeError('method_not_found', 'No worker handler installed')
            result = self.handler(value['method'], value['params'])
            response = {'id': value['id'], 'result': result}
        except Exception as exc:
            response = {'id': value['id'], 'error': {'code': getattr(exc, 'code', 'worker_error'), 'message': str(exc)}}
        try:
            self._send(response)
            if self.after_response is not None:
                self.after_response(value['method'])
        except Exception:
            self.close()

    def _read_loop(self):
        try:
            while not self.closed:
                value = read_frame(self.transport)
                request_id = value.get('id')
                if not isinstance(request_id, str) or len(request_id) > 80:
                    raise BridgeError('invalid_frame', 'Invalid IPC request identity')
                if 'method' in value:
                    if (set(value) != {'id', 'method', 'params'} or
                            not request_id.startswith(self.peer_prefix + ':') or
                            not isinstance(value['method'], str) or not isinstance(value['params'], dict)):
                        raise BridgeError('invalid_frame', 'Invalid IPC request')
                    if request_id in self._incoming:
                        raise BridgeError('duplicate_request', 'Repeated IPC request identity')
                    self._incoming.add(request_id)
                    lifecycle = value['method'] in ('mcp.start', 'mcp.stop', 'mcp.status', 'worker.stop')
                    pool = self._control_pool if lifecycle else self._pool
                    pool.submit(self._serve, value)
                else:
                    if (not request_id.startswith(self.prefix + ':') or
                            set(value) not in ({'id', 'result'}, {'id', 'error'})):
                        raise BridgeError('invalid_frame', 'Invalid IPC response')
                    with self._lock:
                        future = self._pending.pop(request_id, None)
                    if future is None:  # A late reply to a timed-out request is never replayed.
                        continue
                    if 'error' in value:
                        error = value['error']
                        if not isinstance(error, dict):
                            future.set_exception(BridgeError('invalid_frame', 'Invalid IPC error'))
                        else:
                            future.set_exception(BridgeError(error.get('code', 'base_error'), error.get('message', 'Base request failed')))
                    else:
                        future.set_result(value['result'])
        except Exception:
            pass
        finally:
            self.close()

    def close(self):
        with self._lock:
            if self.closed:
                return
            self._closed.set()
            pending, self._pending = self._pending, {}
        self.transport.close()
        for future in pending.values():
            if not future.done():
                future.set_exception(BridgeError('disconnected', 'Base connection closed; no retry was issued'))
        self._pool.shutdown(wait=False, cancel_futures=True)
        self._control_pool.shutdown(wait=False, cancel_futures=True)


class WindowsPipe:
    """Overlapped read/write permits a reply while the reader awaits a request."""
    def __init__(self, name, parent_pid):
        import ctypes
        from ctypes import wintypes as wt
        if os.name != 'nt' or not isinstance(name, str) or not name.startswith('\\\\.\\pipe\\') or '/' in name:
            raise ValueError('A local Windows named pipe is required')
        self.ctypes, self.wt = ctypes, wt
        self.k = k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
        k.CreateFileW.restype = wt.HANDLE
        k.WaitNamedPipeW.argtypes = [wt.LPCWSTR, wt.DWORD]
        k.GetNamedPipeServerProcessId.argtypes = [wt.HANDLE, ctypes.POINTER(wt.ULONG)]
        k.CreateEventW.argtypes = [ctypes.c_void_p, wt.BOOL, wt.BOOL, wt.LPCWSTR]
        k.CreateEventW.restype = wt.HANDLE
        k.CloseHandle.argtypes = [wt.HANDLE]
        k.CancelIoEx.argtypes = [wt.HANDLE, ctypes.c_void_p]
        k.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
        k.GetOverlappedResult.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(wt.DWORD), wt.BOOL]
        k.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
        k.WriteFile.argtypes = k.ReadFile.argtypes
        class Overlapped(ctypes.Structure):
            _fields_ = [('Internal', ctypes.c_size_t), ('InternalHigh', ctypes.c_size_t),
                        ('Offset', wt.DWORD), ('OffsetHigh', wt.DWORD), ('hEvent', wt.HANDLE)]
        self.Overlapped = Overlapped
        self._lock = threading.Lock()
        self.handle = None
        k.WaitNamedPipeW(name, 10000)
        handle = k.CreateFileW(name, 0xc0000000, 0, None, 3, 0x40000000, None)
        if handle == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_last_error(), 'Cannot connect to Base pipe')
        server = wt.ULONG()
        if not k.GetNamedPipeServerProcessId(handle, ctypes.byref(server)) or server.value != parent_pid:
            k.CloseHandle(handle)
            raise BridgeError('identity', 'Pipe server is not the specified Base process')
        self.handle = handle

    def _io(self, value, writing):
        c, k, wt = self.ctypes, self.k, self.wt
        with self._lock:
            handle = self.handle
        if handle is None:
            raise BridgeError('disconnected', 'Base pipe is closed')
        size = len(value) if writing else value
        buffer = c.create_string_buffer(value, size) if writing else c.create_string_buffer(size)
        op, count = self.Overlapped(), wt.DWORD()
        op.hEvent = k.CreateEventW(None, True, False, None)
        if not op.hEvent:
            raise OSError(c.get_last_error(), 'Cannot create pipe operation event')
        try:
            method = k.WriteFile if writing else k.ReadFile
            if not method(handle, buffer, size, c.byref(count), c.byref(op)):
                error = c.get_last_error()
                if error != 997:  # ERROR_IO_PENDING
                    raise OSError(error, 'Base pipe I/O failed')
                if k.WaitForSingleObject(op.hEvent, 0xffffffff) != 0:
                    raise OSError(c.get_last_error(), 'Base pipe wait failed')
                if not k.GetOverlappedResult(handle, c.byref(op), c.byref(count), False):
                    raise OSError(c.get_last_error(), 'Base pipe I/O interrupted')
            return count.value if writing else buffer.raw[:count.value]
        finally:
            k.CloseHandle(op.hEvent)

    def read(self, size):
        return self._io(size, False)

    def write(self, data):
        return self._io(data, True)

    def close(self):
        with self._lock:
            handle, self.handle = self.handle, None
        if handle is not None:
            self.k.CancelIoEx(handle, None)
            self.k.CloseHandle(handle)
