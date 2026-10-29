#include "arken_cloudflare_client_v1/qualification.hpp"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
namespace arken::cloudflare::v1 {
namespace C=arken::cloudflare::v1;
void RunQualificationProcess(const C::fs::path& executable,const std::vector<std::string>& args,const C::fs::path& log,unsigned timeoutSeconds){
 C::Require(timeoutSeconds>0&&timeoutSeconds<=600,"qualification_timeout_invalid");
 const int fd=open(log.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 C::Require(fd>=0,"qualification_log_create_failed");
 posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
 int error=posix_spawn_file_actions_init(&actions);
 if(error){close(fd);throw std::runtime_error("qualification_spawn_init_failed");}
 error=posix_spawnattr_init(&attributes);
 if(error){posix_spawn_file_actions_destroy(&actions);close(fd);throw std::runtime_error("qualification_spawn_init_failed");}
 error=posix_spawn_file_actions_adddup2(&actions,fd,STDOUT_FILENO);
 if(!error)error=posix_spawn_file_actions_adddup2(&actions,fd,STDERR_FILENO);
 if(!error)error=posix_spawn_file_actions_addclose(&actions,fd);
 if(!error)error=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
 if(!error)error=posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
 if(!error)error=posix_spawnattr_setpgroup(&attributes,0);
 std::vector<std::string> storage{executable.string()};storage.insert(storage.end(),args.begin(),args.end());
 std::vector<char*> argv;for(auto& arg:storage)argv.push_back(arg.data());argv.push_back(nullptr);
 pid_t pid=0;
 if(!error)error=posix_spawn(&pid,executable.c_str(),&actions,&attributes,argv.data(),environ);
 posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);
 if(error){close(fd);throw std::runtime_error("qualification_spawn_failed");}
 // The unreaped child reserves this PID/process-group identity. Only this
 // exact spawn and its descendants are subject to the bounded deadline.
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeoutSeconds);
 int status=0;bool limited=false;
 for(;;){
  const auto done=waitpid(pid,&status,WNOHANG);
  if(done==pid)break;
  if(done<0){if(errno==EINTR)continue;close(fd);throw std::runtime_error("qualification_wait_failed");}
  struct stat st{};
  if(fstat(fd,&st)!=0||st.st_size>16*1024*1024||std::chrono::steady_clock::now()>=deadline){
   limited=true;const auto signalled=kill(-pid,SIGKILL);
   if(signalled!=0&&errno!=ESRCH){close(fd);throw std::runtime_error("qualification_child_cleanup_failed");}
   while(waitpid(pid,&status,0)<0){if(errno!=EINTR){close(fd);throw std::runtime_error("qualification_wait_failed");}}
   break;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
 }
 close(fd);C::Require(!limited,"qualification_execution_limit");
 C::Require(WIFEXITED(status)&&WEXITSTATUS(status)==0,"qualification_recipe_failed_see_log");
}
}
