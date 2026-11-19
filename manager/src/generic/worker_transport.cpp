#include "worker_transport.h"
#include "common.h"
#include "wire_json.h"
#include <sddl.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace worker_transport {
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t max_frame=8*1024*1024,max_outgoing=16*1024*1024;
constexpr uint32_t max_timeout=300000;
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE h):value(h){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    ~Handle(){reset();}
    explicit operator bool()const{return value&&value!=INVALID_HANDLE_VALUE;}
    void reset(HANDLE h=nullptr){if(*this)CloseHandle(value);value=h;}
};
struct Security {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    Security(){
        Handle token;HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw std::runtime_error("Cannot identify pipe owner");token.reset(raw);
        DWORD size=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&size);std::vector<unsigned char> bytes(size);
        if(!size||!GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size))throw std::runtime_error("Cannot query pipe owner");
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid))throw std::runtime_error("Cannot format pipe owner");
        const std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("Cannot protect worker pipe");attributes.lpSecurityDescriptor=descriptor;
    }
    ~Security(){if(descriptor)LocalFree(descriptor);}
};
std::string random_hex(size_t count){
    std::vector<unsigned char> data(count);if(BCryptGenRandom(nullptr,data.data(),static_cast<ULONG>(count),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Secure nonce unavailable");
    const char* alphabet="0123456789abcdef";std::string result;for(auto value:data){result+=alphabet[value>>4];result+=alphabet[value&15];}return result;
}
bool same_secret(const std::string& a,const std::string& b){
    if(a.size()!=b.size())return false;unsigned difference=0;for(size_t i=0;i<a.size();++i)difference|=static_cast<unsigned char>(a[i]^b[i]);return difference==0;
}
std::wstring quote(const std::wstring& input){
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:input){if(c==L'\\'){++slashes;continue;}result.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
std::vector<wchar_t> environment(const std::wstring& name,const std::wstring& token){
    auto* block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot copy worker environment");
    std::vector<std::wstring> entries;
    for(const wchar_t* entry=block;*entry;entry+=std::wcslen(entry)+1){const std::wstring value=entry;const auto equal=value.find(L'=');
        if(equal==std::wstring::npos||_wcsicmp(value.substr(0,equal).c_str(),name.c_str()))entries.push_back(value);}
    FreeEnvironmentStringsW(block);entries.push_back(name+L"="+token);
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> result;for(const auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}result.push_back(0);
    if(result.size()>1024*1024)throw std::runtime_error("Worker environment exceeds limit");return result;
}
WireJson parse(const std::string& input){
    if(input.empty()||input.size()>max_frame||input.find('\0')!=std::string::npos)throw std::runtime_error("Invalid frame length");
    std::vector<std::set<std::string>> keys;
    return WireJson::parse(input,[&](int depth,WireJson::parse_event_t event,WireJson& value){
        if(depth>64)throw std::runtime_error("JSON depth exceeded");
        if(event==WireJson::parse_event_t::object_start)keys.emplace_back();else if(event==WireJson::parse_event_t::object_end)keys.pop_back();
        else if(event==WireJson::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw std::runtime_error("Duplicate JSON field");return true;
    });
}
bool method_name(const std::string& name){return !name.empty()&&name.size()<=128&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
uint64_t sequence(const std::string& id,char prefix){
    if(id.size()<3||id.size()>22||id[0]!=prefix||id[1]!=':'||id[2]=='0'||id.find_first_not_of("0123456789",2)!=std::string::npos)throw std::runtime_error("Invalid request ID");
    return std::stoull(id.substr(2));
}
WireJson error_result(const char* reason,const std::string& message={},bool uncertain=false){
    WireJson result={{"transport_error",true},{"state",uncertain?"uncertain":"failed"},{"reason",reason},
        {"engine_invoked",uncertain?WireJson(nullptr):WireJson(false)},{"retry_safe",false}};
    if(!message.empty())result["message"]=message.substr(0,2048);return result;
}
DWORD remaining(Clock::time_point deadline){
    if(deadline==Clock::time_point::max())return INFINITE;
    const auto count=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
    return count<=0?0:static_cast<DWORD>(std::min<int64_t>(count,MAXDWORD-1));
}
std::vector<unsigned char> frame(const WireJson& value){
    const auto text=value.dump();if(text.empty()||text.size()>max_frame)throw std::runtime_error("IPC reply exceeds frame limit");
    const auto size=static_cast<uint32_t>(text.size());std::vector<unsigned char> result(4+text.size());
    for(unsigned i=0;i<4;++i)result[i]=static_cast<unsigned char>((size>>(8*i))&255);
    std::copy(text.begin(),text.end(),result.begin()+4);return result;
}
thread_local const void* handler_session=nullptr;
}

struct Transport::Impl {
    struct Session:std::enable_shared_from_this<Session> {
        struct Pending {bool done=false;WireJson result;std::condition_variable ready;};
        Options options;
        Handle pipe,process,job,stop_event;
        DWORD child_pid=0;
        std::string session_id,last_error;
        mutable std::mutex mutex;
        std::condition_variable changed,work_ready,write_ready;
        bool authenticated=false,closing=false,closed=false;
        size_t threads=0,active_handlers=0,outgoing_bytes=0;
        uint64_t next_request=0,last_incoming=0,timeouts=0;
        std::set<uint64_t> seen;
        std::map<std::string,std::shared_ptr<Pending>> pending;
        std::deque<WireJson> work;
        std::deque<std::vector<unsigned char>> outgoing;

        explicit Session(Options value):options(std::move(value)),stop_event(CreateEventW(nullptr,TRUE,FALSE,nullptr)){
            if(!stop_event)throw std::runtime_error("Cannot create worker stop event");
        }
        void abort(const std::string& reason){
            std::lock_guard<std::mutex> lock(mutex);if(closed)return;closed=true;last_error=reason.substr(0,2048);
            SetEvent(stop_event.value);if(pipe)CancelIoEx(pipe.value,nullptr);job.reset();
            for(auto& item:pending){item.second->done=true;item.second->result=error_result("worker_disconnected",last_error,true);item.second->ready.notify_all();}pending.clear();
            work.clear();outgoing.clear();outgoing_bytes=0;work_ready.notify_all();write_ready.notify_all();changed.notify_all();
        }
        bool io(void* data,size_t size,bool writing,Clock::time_point deadline){
            auto* bytes=static_cast<unsigned char*>(data);size_t offset=0;
            while(offset<size){
                Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)return false;OVERLAPPED operation{};operation.hEvent=event.value;
                DWORD count=0;const auto chunk=static_cast<DWORD>(std::min<size_t>(size-offset,64*1024));
                const BOOL immediate=writing?WriteFile(pipe.value,bytes+offset,chunk,&count,&operation):ReadFile(pipe.value,bytes+offset,chunk,&count,&operation);
                if(!immediate){
                    if(GetLastError()!=ERROR_IO_PENDING)return false;
                    HANDLE handles[]={event.value,stop_event.value,process.value};const auto wait=WaitForMultipleObjects(3,handles,FALSE,remaining(deadline));
                    if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe.value,&operation);GetOverlappedResult(pipe.value,&operation,&count,TRUE);return false;}
                    if(!GetOverlappedResult(pipe.value,&operation,&count,FALSE))return false;
                }
                if(!count)return false;offset+=count;
            }
            return true;
        }
        WireJson read_frame(Clock::time_point deadline){
            unsigned char length[4]{};if(!io(length,4,false,deadline))throw std::runtime_error("Worker pipe closed or timed out");
            uint32_t size=0;for(unsigned i=0;i<4;++i)size|=uint32_t(length[i])<<(8*i);
            if(!size||size>max_frame)throw std::runtime_error("Worker frame length rejected");
            std::string text(size,'\0');const auto body_deadline=std::min(deadline,Clock::now()+std::chrono::seconds(30));
            if(!io(text.data(),text.size(),false,body_deadline))throw std::runtime_error("Worker frame interrupted");
            auto result=parse(text);if(!result.is_object())throw std::runtime_error("Worker frame must be an object");return result;
        }
        bool send(WireJson value){
            auto bytes=frame(value);bool saturated=false;
            {std::lock_guard<std::mutex> lock(mutex);if(closed)return false;
                saturated=outgoing.size()>=64||bytes.size()>max_outgoing-outgoing_bytes;
                if(!saturated){outgoing_bytes+=bytes.size();outgoing.push_back(std::move(bytes));write_ready.notify_one();}}
            if(saturated)abort("Worker outgoing queue saturated");return !saturated;
        }
        void launch(std::function<void()> function){
            auto self=shared_from_this();{std::lock_guard<std::mutex> lock(mutex);++threads;}
            try{std::thread([self,function=std::move(function)]{
                try{function();}catch(const std::exception& error){self->abort(error.what());}catch(...){self->abort("Worker thread failure");}
                std::lock_guard<std::mutex> lock(self->mutex);--self->threads;self->changed.notify_all();
            }).detach();}catch(...){std::lock_guard<std::mutex> lock(mutex);--threads;throw;}
        }
        void writer(){
            for(;;){std::vector<unsigned char> data;
                {std::unique_lock<std::mutex> lock(mutex);write_ready.wait(lock,[&]{return closed||!outgoing.empty();});if(closed)return;
                    data=std::move(outgoing.front());outgoing.pop_front();outgoing_bytes-=data.size();}
                if(!io(data.data(),data.size(),true,Clock::now()+std::chrono::seconds(30)))throw std::runtime_error("Worker write interrupted");
            }
        }
        void handler(){
            for(;;){WireJson request;
                {std::unique_lock<std::mutex> lock(mutex);work_ready.wait(lock,[&]{return closed||!work.empty();});if(closed)return;request=std::move(work.front());work.pop_front();++active_handlers;}
                WireJson response={{"id",request.at("id")}};
                handler_session=this;
                try{response["result"]=parse(options.dispatch(options.user,request.at("method").get<std::string>(),request.at("params").dump()));}
                catch(const RpcError& error){response["error"]={{"code",error.code},{"message",std::string(error.what()).substr(0,2048)}};}
                catch(const std::exception& error){response["error"]={{"code","base_error"},{"message",std::string(error.what()).substr(0,2048)}};}
                catch(...){response["error"]={{"code","base_error"},{"message","Base handler failed"}};}
                handler_session=nullptr;
                try{send(std::move(response));}catch(...){abort("Cannot encode Base response");}
                {std::lock_guard<std::mutex> lock(mutex);--active_handlers;changed.notify_all();}
            }
        }
        void reader(){
            for(;;){auto value=read_frame(Clock::time_point::max());const auto id=value.at("id").get<std::string>();
                if(value.contains("method")){
                    if(value.size()!=3||!value.contains("params")||!value.at("params").is_object()||!value.at("method").is_string())throw std::runtime_error("Invalid worker request");
                    const auto method=value.at("method").get<std::string>();if(!method_name(method)||method=="hello")throw std::runtime_error("Invalid worker method");
                    const auto number=sequence(id,'w');bool busy=false;
                    {std::lock_guard<std::mutex> lock(mutex);if(closed)return;
                        if((last_incoming>4096&&number<=last_incoming-4096)||!seen.insert(number).second)throw std::runtime_error("Replayed worker request");
                        last_incoming=std::max(last_incoming,number);while(!seen.empty()&&last_incoming>4096&&*seen.begin()<=last_incoming-4096)seen.erase(seen.begin());
                        busy=work.size()>=16;if(!busy){work.push_back(std::move(value));work_ready.notify_one();}}
                    if(busy)send({{"id",id},{"error",{{"code","busy"},{"message","Base request queue is full"}}}});
                }else{
                    sequence(id,'b');if(value.size()!=2||(value.contains("result")==value.contains("error")))throw std::runtime_error("Invalid worker response");
                    if(value.contains("error")&&(!value.at("error").is_object()||!value.at("error").contains("code")||!value.at("error").contains("message")||!value.at("error").at("message").is_string()||
                        (!value.at("error").at("code").is_string()&&!value.at("error").at("code").is_number_integer())))throw std::runtime_error("Invalid worker error");
                    std::lock_guard<std::mutex> lock(mutex);const auto found=pending.find(id);if(found==pending.end())continue;
                    auto reply=found->second;reply->result=value.contains("result")?value.at("result"):error_result("worker_rpc_error",value.at("error").at("message").get<std::string>(),true);
                    if(value.contains("error"))reply->result["code"]=value.at("error").at("code");reply->done=true;pending.erase(found);reply->ready.notify_all();
                }
            }
        }
        void bootstrap(){
            const auto deadline=Clock::now()+std::chrono::milliseconds(options.startup_timeout_ms);
            const auto token=random_hex(32);{std::lock_guard<std::mutex> lock(mutex);session_id=random_hex(16);}
            const auto pipe_name=L"\\\\.\\pipe\\san14-base-"+std::to_wstring(options.parent_pid)+L"-"+wide(random_hex(16));
            const auto token_name=L"SAN14_BASE_TOKEN_"+wide(random_hex(12));Security security;
            pipe.reset(CreateNamedPipeW(pipe_name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,64*1024,64*1024,0,&security.attributes));
            if(!pipe)throw std::runtime_error("Cannot create private worker pipe");
            job.reset(CreateJobObjectW(nullptr,nullptr));if(!job)throw std::runtime_error("Cannot create worker Job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot protect worker lifetime");
            auto env=environment(token_name,wide(token));std::wstring command=quote(options.worker_exe);
            for(const auto& argument:options.worker_args)command+=L" "+quote(argument);
            command+=L" --pipe "+quote(pipe_name)+L" --token-env "+quote(token_name)+L" --parent-pid "+std::to_wstring(options.parent_pid);
            if(command.size()>=32767)throw std::runtime_error("Worker command line exceeds limit");
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
            if(!CreateProcessW(options.worker_exe.c_str(),command.data(),nullptr,nullptr,FALSE,
                CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,env.data(),options.cwd.c_str(),&startup,&child))throw std::runtime_error("Cannot launch managed worker");
            process.reset(child.hProcess);Handle primary_thread(child.hThread);{std::lock_guard<std::mutex> lock(mutex);child_pid=child.dwProcessId;}
            if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("Cannot assign worker Job");}
            Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!connected)throw std::runtime_error("Cannot create pipe connection event");
            OVERLAPPED connection{};connection.hEvent=connected.value;bool wait=false;
            if(!ConnectNamedPipe(pipe.value,&connection)){
                const auto code=GetLastError();if(code==ERROR_IO_PENDING)wait=true;else if(code!=ERROR_PIPE_CONNECTED)throw std::runtime_error("Cannot accept worker connection");
            }
            if(ResumeThread(primary_thread.value)==static_cast<DWORD>(-1)){
                if(wait){DWORD ignored=0;CancelIoEx(pipe.value,&connection);GetOverlappedResult(pipe.value,&connection,&ignored,TRUE);}throw std::runtime_error("Cannot resume managed worker");}
            if(wait){HANDLE handles[]={connected.value,process.value,stop_event.value};
                const auto result=WaitForMultipleObjects(3,handles,FALSE,remaining(deadline));DWORD ignored=0;
                if(result!=WAIT_OBJECT_0){CancelIoEx(pipe.value,&connection);GetOverlappedResult(pipe.value,&connection,&ignored,TRUE);throw std::runtime_error("Worker connection timed out or exited");}
                if(!GetOverlappedResult(pipe.value,&connection,&ignored,FALSE))throw std::runtime_error("Worker connection failed");
            }
            ULONG peer=0;if(!GetNamedPipeClientProcessId(pipe.value,&peer)||peer!=child_pid)throw std::runtime_error("Worker pipe PID mismatch");
            auto hello=read_frame(deadline);
            if(hello.size()!=3||hello.at("method")!="hello"||!hello.at("params").is_object())throw std::runtime_error("Worker handshake rejected");
            const auto id=hello.at("id").get<std::string>();const auto hello_sequence=sequence(id,'w');const auto& params=hello.at("params");
            if(params.size()!=4||params.at("protocol")!=1||params.at("parent_pid")!=options.parent_pid||params.at("worker_pid")!=child_pid||
                !params.at("protocol").is_number_integer()||!params.at("parent_pid").is_number_integer()||!params.at("worker_pid").is_number_integer()||
                !same_secret(params.at("token").get<std::string>(),token))throw std::runtime_error("Worker authentication failed");
            auto reply=frame({{"id",id},{"result",{{"protocol",1},{"session_id",session_id}}}});
            if(!io(reply.data(),reply.size(),true,deadline))throw std::runtime_error("Worker authentication reply failed");
            {std::lock_guard<std::mutex> lock(mutex);authenticated=true;last_incoming=hello_sequence;seen.insert(hello_sequence);}
            auto self=shared_from_this();launch([self]{self->writer();});for(unsigned i=0;i<12;++i)launch([self]{self->handler();});launch([self]{self->reader();});
        }
        WireJson call(const std::string& method,const WireJson& params,uint32_t timeout,bool control=false){
            std::string id;std::shared_ptr<Pending> result;
            {std::lock_guard<std::mutex> lock(mutex);
                if(closed||!authenticated)return error_result("worker_disconnected");
                if(closing&&!control&&handler_session!=this&&method!="mcp.status"&&method!="mcp.stop")return error_result("worker_stopping");
                if(pending.size()>=(control?13u:12u))return error_result("worker_busy");
                id="b:"+std::to_string(++next_request);result=std::make_shared<Pending>();pending.emplace(id,result);
            }
            try{if(!send({{"id",id},{"method",method},{"params",params}}))return error_result("worker_disconnected",{},true);}
            catch(const std::exception& error){std::lock_guard<std::mutex> lock(mutex);pending.erase(id);return error_result("worker_invalid_request",error.what());}
            std::unique_lock<std::mutex> lock(mutex);
            if(!result->ready.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return result->done;})){++timeouts;auto uncertain=error_result("worker_timeout",{},true);uncertain["request_id"]=id;return uncertain;}
            return result->result;
        }
    };
    Options options;
    mutable std::mutex mutex;
    std::timed_mutex lifecycle;
    std::shared_ptr<Session> session;
    explicit Impl(Options value):options(std::move(value)){if(!options.parent_pid)options.parent_pid=GetCurrentProcessId();}
    bool forbidden()const{try{return options.forbidden_thread&&options.forbidden_thread(options.user);}catch(...){return true;}}
};

Transport::Transport(Options options):impl_(std::make_shared<Impl>(std::move(options))){}
Transport::~Transport(){std::shared_ptr<Impl::Session> session;{std::lock_guard<std::mutex> lock(impl_->mutex);session=impl_->session;}if(session)session->abort("Base transport owner closed");}
bool Transport::start(std::string& error){
    error.clear();auto self=impl_;if(self->forbidden()){error="worker_wrong_thread";return false;}
    const auto& options=self->options;
    if(!options.dispatch||options.parent_pid!=GetCurrentProcessId()||!options.startup_timeout_ms||options.startup_timeout_ms>max_timeout){error="worker_invalid_options";return false;}
    std::unique_lock<std::timed_mutex> operation(self->lifecycle,std::defer_lock);
    if(!operation.try_lock_for(std::chrono::milliseconds(options.startup_timeout_ms))){error="worker_busy";return false;}
    try{
        const fs::path exe(options.worker_exe),cwd(options.cwd);
        if(!exe.is_absolute()||!cwd.is_absolute()||exe.root_name().wstring().size()!=2||cwd.root_name().wstring().size()!=2||
           options.worker_exe.find(L'\0')!=std::wstring::npos||options.cwd.find(L'\0')!=std::wstring::npos||!fs::is_regular_file(exe)||!fs::is_directory(cwd))throw std::runtime_error("Worker paths must be admitted local absolute paths");
        for(const auto& arg:options.worker_args)if(arg.find(L'\0')!=std::wstring::npos||arg==L"--pipe"||arg==L"--token-env"||arg==L"--parent-pid")throw std::runtime_error("Reserved worker argument");
        {std::lock_guard<std::mutex> lock(self->mutex);if(self->session){std::lock_guard<std::mutex> state(self->session->mutex);
            if(!self->session->closed)return self->session->authenticated;
            if(self->session->threads||(self->session->process&&WaitForSingleObject(self->session->process.value,0)!=WAIT_OBJECT_0)){error="worker_cleanup_pending";return false;}}}
        auto current=std::make_shared<Impl::Session>(options);{std::lock_guard<std::mutex> lock(self->mutex);self->session=current;}
        try{current->bootstrap();}catch(...){current->abort("Worker startup failed");throw;}return true;
    }catch(const std::exception& caught){error=std::string(caught.what()).substr(0,2048);return false;}
}
std::string Transport::request(const std::string& method,const std::string& params_json,uint32_t timeout_ms){
    auto self=impl_;if(self->forbidden())return error_result("worker_wrong_thread").dump();
    if(!method_name(method)||method=="hello"||!timeout_ms||timeout_ms>max_timeout)return error_result("worker_invalid_request").dump();
    try{
        const auto params=parse(params_json);if(!params.is_object())return error_result("worker_invalid_request").dump();
        std::shared_ptr<Impl::Session> current;{std::lock_guard<std::mutex> lock(self->mutex);current=self->session;}
        return current?current->call(method,params,timeout_ms).dump():error_result("worker_not_started").dump();
    }catch(const std::exception& caught){return error_result("worker_invalid_request",caught.what()).dump();}
}
bool Transport::stop(uint32_t timeout_ms,std::string& error){
    error.clear();auto self=impl_;if(self->forbidden()){error="worker_wrong_thread";return false;}
    if(!timeout_ms||timeout_ms>max_timeout){error="worker_invalid_timeout";return false;}
    const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);std::unique_lock<std::timed_mutex> operation(self->lifecycle,std::defer_lock);
    if(!operation.try_lock_until(deadline)){error="worker_busy";return false;}
    std::shared_ptr<Impl::Session> current;{std::lock_guard<std::mutex> lock(self->mutex);current=self->session;}if(!current)return true;
    bool already_closed=false;{std::lock_guard<std::mutex> lock(current->mutex);already_closed=current->closed;current->closing=true;}
    if(!already_closed){const auto reply=current->call("worker.stop",WireJson::object(),std::max<DWORD>(1,remaining(deadline)),true);
        if(reply.is_object()&&reply.value("transport_error",false)&&reply.value("reason",std::string())!="worker_disconnected"){error=reply.value("reason",std::string("worker_stop_failed"));return false;}}
    if(current->process&&WaitForSingleObject(current->process.value,remaining(deadline))!=WAIT_OBJECT_0){error="worker_busy";return false;}
    current->abort("Worker stopped");std::unique_lock<std::mutex> lock(current->mutex);
    if(!current->changed.wait_until(lock,deadline,[&]{return current->threads==0;})){error="worker_handlers_busy";return false;}return true;
}
std::string Transport::status()const{
    std::shared_ptr<Impl::Session> current;{std::lock_guard<std::mutex> lock(impl_->mutex);current=impl_->session;}
    if(!current)return WireJson{{"state","stopped"},{"worker_pid",0}}.dump();
    std::lock_guard<std::mutex> lock(current->mutex);
    return WireJson{{"state",current->closed?"closed":current->closing?"stopping":current->authenticated?"running":"starting"},
        {"worker_pid",current->child_pid},{"parent_pid",current->options.parent_pid},{"session_id",current->session_id},
        {"pending_requests",current->pending.size()},{"queued_handlers",current->work.size()},{"active_handlers",current->active_handlers},
        {"threads",current->threads},{"timeouts",current->timeouts},{"last_error",current->last_error}}.dump();
}
}
