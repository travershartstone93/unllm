#include "unllm/symbolic.h"
#ifdef UNLLM_SYMBOLIC_READY
#include "unllm/json.h"
#include "unllm/native_layout.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace unllm {
struct Symbolic::Impl {
    pid_t child=-1;
    int input=-1, output=-1;
    std::string pending;
    ~Impl(){ stop(); }
    void stop() {
        if(input>=0) close(input);
        if(output>=0) close(output);
        input=output=-1;
        pending.clear();
        if(child>0) {
            kill(-child,SIGTERM);
            for(int n=0;n<20;++n) {
                if(waitpid(child,nullptr,WNOHANG)==child){child=-1;return;}
                usleep(10000);
            }
            kill(-child,SIGKILL); waitpid(child,nullptr,0);
        }
        child=-1;
    }
    void start() {
        if(child>0) return;
        const bool native_mode=NativeLayout::selected();
        NativeLayout layout{};
        if(native_mode)
            layout=NativeLayout::locate(std::filesystem::canonical("/proc/self/exe").parent_path(), UNLLM_UPGRADE_ROOT);
        int in[2],out[2];
        if(pipe2(in,O_CLOEXEC)!=0) throw std::runtime_error("Cannot create symbolic input pipe");
        if(pipe2(out,O_CLOEXEC)!=0){close(in[0]);close(in[1]);throw std::runtime_error("Cannot create symbolic output pipe");}
        const pid_t parent=getpid();
        child=fork();
        if(child==0) {
            prctl(PR_SET_PDEATHSIG,SIGTERM);
            if(getppid()!=parent)_exit(1);
            setpgid(0,0);
            dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);
            close(in[0]);close(in[1]);close(out[0]);close(out[1]);
            const std::string root=UNLLM_UPGRADE_ROOT;
            if (native_mode) {
                const auto native=layout.bin/"unllm_symbolic_worker";
                if(layout.installed) {
                    execl(native.c_str(),native.c_str(),"--events",(char*)nullptr);
                    _exit(127);
                }
                const auto logic=layout.bin/"unllm_logic_worker";
                const auto db=getenv("UNLLM_FACT_DB")?std::string(getenv("UNLLM_FACT_DB")):root+"/.state/facts.sqlite";
                if (!getenv("UNLLM_ARTIFACTS")) setenv("UNLLM_ARTIFACTS",(root+"/.artifacts").c_str(),1);
                execl(native.c_str(),native.c_str(),root.c_str(),db.c_str(),logic.c_str(),"--events",(char*)nullptr);
                _exit(127);
            }
#ifndef UNLLM_DEFAULT_NATIVE_RUNTIME
            const std::string python=root+"/.venv/bin/python";
            const std::string worker=root+"/tools/symbolic_worker.py";
            setenv("UNLLM_LOGIC_LIBRARY",UNLLM_LOGIC_LIBRARY,1);
            execl(python.c_str(),python.c_str(),"-u",worker.c_str(),(char*)nullptr);
#endif
            _exit(127);
        }
        close(in[0]);close(out[1]);
        if(child<0){close(in[1]);close(out[0]);throw std::runtime_error("Cannot start symbolic worker");}
        setpgid(child,child);
        input=in[1];output=out[0];
        int flags=fcntl(input,F_GETFL);
        if(flags<0 || fcntl(input,F_SETFL,flags|O_NONBLOCK)<0) {
            stop();
            throw std::runtime_error("Cannot set up symbolic input pipe");
        }
    }
    std::string request(const std::string& data, int timeout_ms) {
        start();
        // A dead worker must become an explicit error rather than terminate
        // the CLI with SIGPIPE.
        signal(SIGPIPE,SIG_IGN);
        const std::string message=data+"\n";
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        auto ready_until=[&](int fd,short events){
            while(true) {
                const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline-std::chrono::steady_clock::now()).count();
                if(ms<=0){stop();throw std::runtime_error("Symbolic request exceeded its deadline");}
                pollfd descriptor{fd,events,0};
                const int ready=poll(&descriptor,1,int(ms));
                if(ready<0 && errno==EINTR)continue;
                if(ready<=0){stop();throw std::runtime_error("Symbolic pipe error or timeout");}
                return;
            }
        };
        for(size_t offset=0;offset<message.size();) {
            const auto n=write(input,message.data()+offset,message.size()-offset);
            if(n<0 && errno==EINTR)continue;
            if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){ready_until(input,POLLOUT);continue;}
            if(n<=0){stop();throw std::runtime_error("Symbolic worker closed its input");}
            offset+=size_t(n);
        }
        bool synthesis_started=false;
        while(true) {
            if(auto nl=pending.find('\n');nl!=std::string::npos) {
                auto result=pending.substr(0,nl);pending.erase(0,nl+1);
                const auto object=json::Value::parse(result);
                if(object["event"].text()=="synthesis_started" && !synthesis_started) {
                    synthesis_started=true;
                    deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(180000);
                    continue;
                }
                return result;
            }
            ready_until(output,POLLIN);
            char buffer[65536];
            const auto n=read(output,buffer,sizeof buffer);
            if(n<0 && errno==EINTR)continue;
            if(n<=0){stop();throw std::runtime_error("Symbolic worker exited");}
            pending.append(buffer,size_t(n));
            if(pending.size()>16*1024*1024){stop();throw std::runtime_error("Symbolic reply exceeded its size budget");}
        }
    }
};
Symbolic::Symbolic():impl_(std::make_unique<Impl>()){}
Symbolic::~Symbolic()=default;
bool Symbolic::enabled() {
    const char* engine=getenv("UNLLM_ENGINE");
    return !engine || std::string(engine)!="legacy";
}
SymbolicReply Symbolic::ask(const std::string& question,int64_t now,const std::string& context) {
    try {
        auto request=json::Value::object();
        request.set("question",json::Value::string(question));
        request.set("now",json::Value::number(now));
        request.set("context",json::Value::string(context));
        if(const char* lang=getenv("UNLLM_CODE_LANGUAGE"))request.set("language",json::Value::string(lang));
        if(question.size()>65536)throw std::runtime_error("Request exceeds the 64 KiB input budget");
        const auto raw=impl_->request(request.dump(),3000);
        const auto reply=json::Value::parse(raw);
        return {reply["text"].text(),raw,reply["status"].text("error")};
    } catch(const std::exception& error) {
        auto reply=json::Value::object();reply.set("status",json::Value::string("error"));
        const std::string text=std::string("Symbolic engine error: ")+error.what();
        reply.set("text",json::Value::string(text));return {text,reply.dump(),"error"};
    }
}
}
#else
#include <cstdlib>
namespace unllm {
struct Symbolic::Impl {};
Symbolic::Symbolic()=default;
Symbolic::~Symbolic()=default;
bool Symbolic::enabled(){return false;}
SymbolicReply Symbolic::ask(const std::string&,int64_t,const std::string&){
    return {"Symbolic support was disabled at build time.","","unsupported"};
}
}
#endif
