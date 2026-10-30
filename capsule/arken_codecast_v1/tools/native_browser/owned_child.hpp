#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <functional>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace arken::codecast::native_browser {
struct ChildStopResult { bool reaped=false; bool forced=false; int status=0; int error=0; unsigned terminationSignals=0; };

// Only a waitable child of this process may be signalled. Reap before every
// escalation so a reused or foreign PID never becomes a cleanup target.
inline bool WaitOwnedChild(pid_t pid,std::chrono::milliseconds timeout,ChildStopResult& result) noexcept {
  const auto deadline=std::chrono::steady_clock::now()+timeout;
  do {
    const auto waited=::waitpid(pid,&result.status,WNOHANG);
    if(waited==pid){result.reaped=true;return true;}
    if(waited<0&&errno!=EINTR){result.error=errno;return true;}
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
    if(left<=std::chrono::milliseconds::zero())return false;
    std::this_thread::sleep_for(std::min(left,std::chrono::milliseconds(10)));
  }while(true);
}

inline ChildStopResult StopOwnedChild(pid_t pid,std::chrono::milliseconds grace=std::chrono::milliseconds(2000),
    std::chrono::milliseconds terminate=std::chrono::milliseconds(2000),std::chrono::milliseconds force=std::chrono::milliseconds(2000),
    const std::function<void(pid_t)>& beforeForce={},unsigned terminateAttempts=1) noexcept {
  ChildStopResult result;
  if(pid<=0||terminateAttempts==0||terminateAttempts>8){result.error=EINVAL;return result;}
  if(WaitOwnedChild(pid,grace,result))return result;
  const auto deadline=std::chrono::steady_clock::now()+terminate;
  // Opt-in retries share one total deadline. Revalidate waitability before
  // every signal; no retry may target a reaped/reused or foreign process.
  for(unsigned attempt=0;attempt<terminateAttempts;++attempt){
    if(WaitOwnedChild(pid,std::chrono::milliseconds(0),result))return result;
    if(::kill(pid,SIGTERM)<0&&errno!=ESRCH){result.error=errno;return result;}
    ++result.terminationSignals;
    const auto left=std::max(std::chrono::milliseconds(0),std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()));
    if(WaitOwnedChild(pid,left/(terminateAttempts-attempt),result))return result;
  }
  // Preserve bounded diagnostics while this exact child remains waitable.
  // The callback must not reap or signal it. Recheck after observation.
  if(beforeForce){try{beforeForce(pid);}catch(...){result.error=EIO;}}
  if(WaitOwnedChild(pid,std::chrono::milliseconds(0),result))return result;
  result.forced=true;
  if(::kill(pid,SIGKILL)<0&&errno!=ESRCH){result.error=errno;return result;}
  if(!WaitOwnedChild(pid,force,result))result.error=ETIMEDOUT;
  return result;
}
}
