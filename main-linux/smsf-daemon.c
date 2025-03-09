/*
 * Copyright (C) 2025 Dmitry Samersoff (dms@samersoff.net)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <execinfo.h>
#include <ucontext.h>


#include "smsf-logging.h"
#include "smsf-daemon.h"

#define BT_BUF_SIZE 10

extern struct smsf_options _opts;

static int write_pid_file(const char *prog_name){
    FILE *pidf;
    long  pid;

    char pidname[PATH_MAX];
    sprintf(pidname,"%s/%s.pid",PID_PATH,prog_name);

  /* first at all, check pid file */
    if (access(pidname,0) != -1) {
        if (!(pidf = fopen(pidname,"r"))) {
        log_errno("Can't read PID file '%s'", pidname);
        return -1;
    }

    fscanf(pidf,"%ld",&pid);
    fclose(pidf);

    if (kill(pid, 0) != -1) {
        log_err("%s alredy run with pid '%ld'", prog_name, (long) pid);
        return -1;
    }
  }
 /* create new pid file */
  if (!(pidf = fopen(pidname,"w"))) {
      log_errno("Can't create PID file '%s'", pidname);
      return -1;
  }

  fprintf(pidf,"%ld",(long) getpid());
  fclose(pidf);
  return 0;
}

static sigfunc_t* set_signal(int signo, sigfunc_t *func) {
  struct sigaction  act, oact;

  act.sa_handler = func;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  if (signo == SIGALRM) {
#ifdef  SA_INTERRUPT
    act.sa_flags |= SA_INTERRUPT;   /* SunOS */
#endif
  }
  else {
#ifdef  SA_RESTART
    act.sa_flags |= SA_RESTART;   /* SVR4, 44BSD */
#endif
  }

  if (sigaction(signo, &act, &oact) < 0) {
    return(SIG_ERR);
  }

  return(oact.sa_handler);
}

static void SIGINT_hdl(int sig){
  if (sig != SIGHUP) {
    exit(-1);
  }
}

static void CRASH_hdl(int sig, siginfo_t* info, ucontext_t* uc) {
    // Restore default handler
    set_signal(SIGABRT, NULL);

    if (uc == NULL) {
      // A bit of paranoia
      abort();
    }

    /* extract key registers */
    void *ip = NULL, *sp = NULL, **bp = NULL;

    #ifdef __linux__
      #ifdef __X86__
        ip  = (void*) uc->uc_mcontext.gregs[REG_EIP];
        sp = (void*)  uc->uc_mcontext.gregs[REG_ESP];
        bp = (void**) uc->uc_mcontext.gregs[REG_EBP];
      #endif

      #ifdef __arm__
        ip = (void*) uc->uc_mcontext.arm_pc;
        sp = (void*) uc->uc_mcontext.arm_sp;
        bp = (void**) uc->uc_mcontext.arm_fp;
      #endif

      #ifdef __aarch64__
        ip = (void*) uc->uc_mcontext.pc;
        sp = (void*) uc->uc_mcontext.sp;
        bp = (void**) uc->uc_mcontext.regs[29];
      #endif
    #endif

    #if defined(__FreeBSD__)
      ip  = (void*) uc->uc_mcontext.mc_rip;
      sp = (void*) uc->uc_mcontext.mc_rsp;
      bp = (void**) uc->uc_mcontext.mc_rbp;
    #endif

    log_info("#");
    log_info("# An unexpected error has been detected:");
    log_info("#");
    log_info("# SIGNAL %d at ip=0x%p, pid=%ld\n", sig,  ip, (long) getpid());
    log_info("#");
    log_info("# %s", _opts.version);
    log_info("#");
    log_info("# Stack: sp=%p\n", sp);

    int nptrs;
    void *buffer[BT_BUF_SIZE];

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    log_info("# Backtrace: %d\n", nptrs);
    char **strs = backtrace_symbols(buffer, nptrs);
    if (strs != NULL) {
        for (int i = 0; i < nptrs; ++i) {
            log_info("%s", strs[i]);
        }
    }
    free(strs);
    abort();
}

void daemonize(const char *prog_name) {
  int res;

  set_signal(SIGTTOU, SIG_IGN);
  set_signal(SIGTTIN, SIG_IGN);
  set_signal(SIGTSTP, SIG_IGN);
  set_signal(SIGTRAP, SIG_IGN);

  set_signal(SIGTERM, SIGINT_hdl);
  set_signal(SIGINT, SIGINT_hdl);
  set_signal(SIGHUP, SIGINT_hdl);

  set_signal(SIGILL,  (sigfunc_t *) CRASH_hdl);
  set_signal(SIGSEGV, (sigfunc_t *) CRASH_hdl);
  set_signal(SIGBUS, (sigfunc_t *) CRASH_hdl);
  set_signal(SIGFPE, (sigfunc_t *) CRASH_hdl);

  if ((res = fork()) < 0) {
      log_errno("Fork error");
      return;
  }

  if (res >  0) {   /* parent must die */
      exit(0);
  }

  if (write_pid_file(prog_name) == -1) {
    // Soft abort
      exit(-1);
  }

  setsid();

  // Сlose first 64 file descriptors
  // TODO: do it better
  for (int i = 0; i < 64; ++i) {
    close(i);
  }

  chdir("/var/tmp");
}

int kill_running(const char *prog_name) {
    FILE *pidf;
    long  pid;
    char pidname[PATH_MAX];
    sprintf(pidname,"%s/%s.pid",PID_PATH, prog_name);

    if (!(pidf = fopen(pidname,"r"))) {
        log_errno("Can't read PID file '%s'", pidname);
        return -1;
    }
    fscanf(pidf,"%ld",&pid);
    fclose(pidf);

    if (kill(pid, 0) == -1) {
        log_errno("Can't kill %s pid: %ld", prog_name, (long) pid);
        return -1;
    }

    log_noise("About to kill %s pid: %ld", prog_name, (long) pid);
    if (pid > 1) { // paranoia
        while(kill(pid, SIGTERM) != -1) {
          usleep(1000);
        }
    }

    if (kill(pid, 0) != -1) {
        log_errno("Can't kill %s pid: %ld", prog_name, (long) pid);
        return -1;
    }
    log_noise("Killed");
    return 0;
}
