#define _GNU_SOURCE
#include <stdlib.h> 
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#include <proc/readproc.h>
#endif

#include "thpool.h"
#include "kvec.h"
#include "t2e.h"

#define ARR "\e[2m\xE2\x97\x82\e[0m"
#define VERSION "0.2.7"

extern char *__progname;
typedef kvec_t(pid_t) kv_t;

#define CHUNK 0xFFFF

typedef struct
{
    int utime_ticks;
    int cutime_ticks;
    int stime_ticks;
    int cstime_ticks;
    int vsize; // virtual memory size in bytes
} pstat_t;

typedef struct
{
	pid_t pid;
	bool use_shm;
	long shm;
	double rss, shr, cpu;
} usg_t;

typedef struct
{
	long unsigned rss, shr;
} mem_t;

typedef struct
{
	unsigned long ts;
	double rss, shr, cpu;
	char cmd[PATH_MAX];
} mn_t;

void ttoa(time_t t);
time_t atot(const char *a);
void stoa(const int sec, char **a);
bool use_shm(const pid_t pid);
bool ends_with(const char *str, const char *sfx);

int pgrep(const char *proc);
int chk_pid(pid_t pid);
int ncpid(const pid_t ppid);
void get_cpid(const pid_t ppid, pid_t *pid);
void pids_of_ppid(const pid_t ppid, kv_t *kv);
void pid_to_name(const pid_t pid, char cmd[PATH_MAX]);

unsigned nprocs();
void calc_usg(void *_usg);
void calc_usgd(const pid_t ppid, mn_t **mns, int *m, int *n, const double shm, FILE *fp);

long size_of(const char *fn);
char *get_now(void);
void get_mem(const pid_t pid, mem_t *mem);
void get_cpu(const pstat_t *cur_usage, const pstat_t *last_usage, int *ucpu_usage, int *scpu_usage);
int get_usg(const pid_t pid, pstat_t* result);
int ndigit(const char *str);

void usage();

int main(int argc, char *argv[])
{
	char *st = get_now();
	int i, m = CHUNK, n = 0;
	mn_t **mns = calloc(m, sizeof(mn_t *));
	pid_t pid = 0;
	if (argc == 1)
	{
		usage();
		free(mns);
		exit(1);
	}
	else if (argc == 2)
	{
		if (!strcmp(basename(argv[0]), basename(argv[1])))
			return 0;
		if (!strncmp(argv[1], "-h", 2) || !strncmp(argv[1], "--h", 3))
		{
			usage();
			free(mns);
			return 0;
		}
		if (!strncmp(argv[1], "-v", 2) || !strncmp(argv[1], "--v", 3))
		{
			puts(VERSION);
			free(mns);
			return 0;
		}
		if (strlen(argv[1]) == ndigit(argv[1])) // process ID
			pid = atoi(argv[1]);
		else
		{
			if (access(argv[1], X_OK))
			{
				fprintf(stderr, "Permission denied for [%s]\n", argv[1]);
				exit(1);
			}
			else
				pid = pgrep(argv[1]);
		}
		if (pid)
		{
			char *log;
			asprintf(&log, "%d.log", pid);
			FILE *fp = fopen(log, "w");
			if (!fp)
			{
				perror("Error opening log file");
				exit(1);
			}
			long shm = size_of("/dev/shm");
			fputs("#TIMESTAMP\tRSS\tSHR\tCPU\tCOMMAND\n", fp);
			while (1)
			{
				if ((kill(pid, 0) == -1 && errno == ESRCH))
					break;
				calc_usgd(pid, mns, &m, &n, shm, fp);
				sleep(2);
			}
			fclose(fp);
			free(log);
		}
	}
	else
	{
		char **args = malloc(sizeof(char *) * argc);
		for (i = 0; i < argc - 1; ++i)
			args[i] = argv[i + 1];
		args[argc - 1] = NULL;
		pid_t fpid = fork();
		if (fpid == 0)
		{
			if (execvp(argv[1], args) < 0)
			{
				perror("Error running command");
				exit(-1);
			}
			free(args);
		}
		else if (fpid > 1)
		{
			int status;
			pid = getpid();
			get_cpid(pid, &pid);
			if (pid)
			{
				char *log;
				asprintf(&log, "%d.log", pid);
				FILE *fp = fopen(log, "w");
				if (!fp)
				{
					perror("Error opening log file");
					exit(1);
				}
				long shm = size_of("/dev/shm");
				fputs("#TIMESTAMP\tRSS\tSHR\tCPU\tCOMMAND\n", fp);
				while (1)
				{
					if ((kill(pid, 0) == -1 && errno == ESRCH))
						break;
					waitpid(pid, &status, WNOHANG|WUNTRACED);
					if (WIFEXITED(status) == 0 && WEXITSTATUS(status) == 0)
						break;
					else
					{
						calc_usgd(pid, mns, &m, &n, shm, fp);
						sleep(2);
					}
				}
				fclose(fp);
				free(log);
			}
		}
		else
		{
			printf("Error: Fork process failed");
			exit(-1);
		}
	}
	if (n)
	{
		char *log;
		asprintf(&log, "%d.log", pid);
		char *ehtml;
		asprintf(&ehtml, "%d.html", pid);
        t2e_t to;
        to.ehtml = ehtml;
        to.tbres = log;
        to.width = 1200;
        to.height = 800;
        t2e_html(&to);
        free(ehtml);
        free(log);
	}
	free(mns);
	free(st);
	return 0;
}

char *get_now(void)
{
	char buf[80];
	time_t now = time(0);
	strftime(buf,sizeof(buf),"%D %X" ,localtime(&now));
	return strdup(buf);
}

/*
 * read /proc data into the passed variables
 * returns 0 on success, -1 on error
*/
void get_mem(const pid_t pid, mem_t *mem)
{
	char *statm;
	asprintf(&statm, "/proc/%d/statm", pid);
	FILE *fpstat = fopen(statm, "r");
	free(statm);
	if (!fpstat) return;
	if (fscanf(fpstat, "%*d %ld %ld %*[^\1]", &mem->rss, &mem->shr) == EOF)
	{
		fclose(fpstat);
		return;
	}
	mem->rss *= getpagesize();
	mem->shr *= getpagesize();
	mem->rss -= mem->shr;
	fclose(fpstat);
}

/*
 * read /proc data into the passed struct pstat
 * returns 0 on success, -1 on error
*/
int get_usg(const pid_t pid, pstat_t* result)
{
	//convert pid to string
	char pid_s[20];
	snprintf(pid_s, sizeof(pid_s), "%d", pid);
	char stat_filepath[30] = "/proc/";
	strncat(stat_filepath, pid_s,
			sizeof(stat_filepath) - strlen(stat_filepath) -1);
	strncat(stat_filepath, "/stat",
			sizeof(stat_filepath) - strlen(stat_filepath) -1);
	FILE *fpstat = fopen(stat_filepath, "r");
	if (!fpstat)
		return -1;
	//read values from /proc/pid/stat
	bzero(result, sizeof(pstat_t));
	if (fscanf(fpstat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
			"%lu %ld %ld %*d %*d %*d %*d %*u %lu %*d",
			&result->utime_ticks, &result->stime_ticks,
			&result->cutime_ticks, &result->cstime_ticks,
			&result->vsize) == EOF)
		return -1;
	fclose(fpstat);
	return 0;
}

/*
* calculates the elapsed CPU usage between 2 measuring points in ticks
*/
void get_cpu(const pstat_t *cur_usage, const pstat_t *last_usage,
		int *ucpu_usage, int *scpu_usage)
{
	*ucpu_usage = (cur_usage->utime_ticks + cur_usage->cutime_ticks) -
			(last_usage->utime_ticks + last_usage->cutime_ticks);
	*scpu_usage = (cur_usage->stime_ticks + cur_usage->cstime_ticks) -
			(last_usage->stime_ticks + last_usage->cstime_ticks);
}

int chk_pid(pid_t pid)
{
	int ok = 1;
	struct stat st;
	char a[NAME_MAX];
	snprintf(a, sizeof(a), "/proc/%d", pid);
	if (stat(a, &st) == -1 && errno == ENOENT)
		ok = 0;
	return ok;
}

int ncpid(const pid_t ppid)
{
	int n = 0;
	static proc_t pinfo;
	memset(&pinfo, 0, sizeof(pinfo));
	PROCTAB *proc = openproc(PROC_FILLSTAT);
	if (!proc) return n;
	while (readproc(proc, &pinfo))
		n += pinfo.ppid == ppid;
	closeproc(proc);
	return n;
}

void get_cpid(const pid_t ppid, pid_t *pid)
{
	static proc_t pinfo;
	memset(&pinfo, 0, sizeof(pinfo));
	PROCTAB *proc = openproc(PROC_FILLSTAT);
	if (!proc) return;
	while (readproc(proc, &pinfo))
		if (pinfo.ppid == ppid)
			*pid = pinfo.tgid;
	closeproc(proc);
}

void pids_of_ppid(const pid_t ppid, kv_t *kv)
{
	static proc_t pinfo;
	int i, j = kv_size(*kv), k;
	memset(&pinfo, 0, sizeof(pinfo));
	PROCTAB *proc = openproc(PROC_FILLSTAT);
	if (!proc) return;
	while (readproc(proc, &pinfo))
		if (pinfo.ppid == ppid)
			kv_push(pid_t, *kv, pinfo.tgid);
	closeproc(proc);
	k = kv_size(*kv);
	for (i = j; i < k; ++i)
		pids_of_ppid(kv_A(*kv, i), kv);
}

int pgrep(const char *proc)
{
	int _pid = 0;
	const char* directory = "/proc";
	char task_name[PATH_MAX];
	DIR *dir = opendir(directory);
	if (dir)
	{
		struct dirent *de = 0;
		while ((de = readdir(dir)) != 0)
		{
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			int pid = -1;
			int res = sscanf(de->d_name, "%d", &pid);
			if (res == 1)
			{
				// we have a valid pid
				// open the cmdline file to determine what's the name of the process running
				char cmdline_file[PATH_MAX] = {0};
				sprintf(cmdline_file, "%s/%d/cmdline", directory, pid);
				FILE *fp = fopen(cmdline_file, "r");
				if (!fp)
					return _pid;
				if (fgets(task_name, PATH_MAX - 1, fp))
				{
					if (!strcmp(basename(task_name), basename(proc)))
					{
						_pid = pid;
						break;
					}
				}
				fclose(fp);
			}
		}
		closedir(dir);
	}
	return _pid;
}

bool use_shm(const pid_t pid)
{
	bool shm = false;
	char cmdline[PATH_MAX] = {0};
	sprintf(cmdline, "/proc/%d/cmdline", pid);
	if (!access(cmdline, R_OK))
	{
		FILE *fp = fopen(cmdline, "r");
		int i;
		size_t sz = 0;
		char *line = NULL;
		if (fp && (i = getline(&line, &sz, fp)) >= 1)
		{
			line[--i] = '\0';
			for (--i; i >= 0; --i)
				if (line[i] == '\0')
					line[i] = ' ';
			shm = (bool)strstr(line, "/dev/shm");
			free(line);
			fclose(fp);
		}
	}
	return shm;
}

long size_of(const char *dirname)
{
	struct stat st;
	DIR *dir = opendir(dirname);
	if (dir == 0)
		return 0;
	struct dirent *dit;
	long size = 0;
	long total_size = 0;
	char path[PATH_MAX];
	while ((dit = readdir(dir)) != NULL)
	{
		if (!strcmp(dit->d_name, ".") || !strcmp(dit->d_name, ".."))
			continue;
		sprintf(path, "%s/%s", dirname, dit->d_name);
		if (lstat(path, &st) != 0)
			continue;
		size = st.st_size;
        if (S_ISDIR(st.st_mode))
		{
			long dir_size = size_of(path) + size;
			total_size += dir_size;
		}
		else
			total_size += size;
	}
	return total_size;
}

bool ends_with(const char *str, const char *sfx)
{
	bool ret = false;
	int str_len = strlen(str);
	int sfx_len = strlen(sfx);
	if ((str_len >= sfx_len) && (0 == strcasecmp(str + (str_len-sfx_len), sfx)))
		ret = true;
	return ret;
}

void pid_to_name(const pid_t pid, char cmd[PATH_MAX])
{
	char cmdline[PATH_MAX] = {0};
	sprintf(cmdline, "/proc/%d/cmdline", pid);
	if (!access(cmdline, R_OK))
	{
		FILE *fp = fopen(cmdline, "r");
		int i, j;
		size_t sz = 0;
		char *line = NULL;
		if (fp && (j = getline(&line, &sz, fp)) >= 1)
		{
			char *p = line;
			while (ends_with(p, "python") ||
					ends_with(p, "python2") ||
					ends_with(p, "python3") ||
					ends_with(p, "perl") ||
					ends_with(p, "java") ||
					ends_with(p, "Rscript") ||
					*p == '-')
				p += strlen(p) + 1;
			strcpy(cmd, basename(p));
			free(line);
			fclose(fp);
		}
		else
			cmd[0] = '\0';
	}
}

unsigned nprocs()
{
	unsigned np = 1;
#ifdef __linux__
	np = get_nprocs();
#else
	np = sysconf(_SC_NPROCESSORS_ONLN);
#endif
	return np;
}

void calc_usg(void *_usg)
{
	usg_t *usg = (usg_t *)_usg;
	pid_t pid = usg->pid;
	if (!chk_pid(pid))
		return;
	int up = 0, idle = 0;
	pstat_t last, current;
	mem_t mem = {0, 0};
	get_mem(pid, &mem);
	int rl = get_usg(pid, &last);
	sleep(1);
	int rc = get_usg(pid, &current);
	get_cpu(&current, &last, &up, &idle);
	usg->rss = mem.rss / pow(1024.0, 3);
	usg->shr = mem.shr / pow(1024.0, 3) + (usg->use_shm ? fmax(0, (size_of("/dev/shm") - usg->shm) / pow(1024.0, 3)) : 0);
	usg->cpu = !(rl + rc) ? up + idle : 0;
}

void calc_usgd(const pid_t ppid, mn_t **mns, int *m, int *n, const double shm, FILE *fp)
{
	int i, j;
	kv_t kv;
	kv_init(kv);
	char cmd[PATH_MAX], *cmds = NULL, *cmds_ascii = NULL;
	pid_to_name(ppid, cmd);
	asprintf(&cmds, "%s", cmd);
	asprintf(&cmds_ascii, "%s", cmd);
	pids_of_ppid(ppid, &kv);
	int kn = kv_size(kv);
	usg_t *usg = calloc(kn + 1, sizeof(usg_t));
	usg[0].pid = ppid;
	usg[0].shm = shm;
	usg[0].use_shm = use_shm(ppid);
	for (i = 0; i < kn; ++i)
	{
		pid_t pid = kv_A(kv, i);
		usg[i + 1].pid = pid;
		usg[i + 1].shm = shm;
		usg[i + 1].use_shm = use_shm(pid);
	}
	i = j = 0;
	threadpool thpool = thpool_init(kn + 1);
	thpool_add_work(thpool, calc_usg, (void *)(uintptr_t)(usg));
	for (i = 0; i < kn; ++i)
	{
		pid_t pid = kv_A(kv, i);
		if (!chk_pid(pid))
			continue;
		pid_to_name(pid, cmd);
		if (strcmp("sh", cmd) && strcmp("bash", cmd) && strcmp("xargs", cmd) &&
				!strstr(cmd, "systemd") && !strstr(cmds, cmd))
		{
			asprintf(&cmds, "%s%s%s", cmds, ARR, cmd);
			asprintf(&cmds_ascii, "%s;%s", cmds_ascii, cmd);
		}
		thpool_add_work(thpool, calc_usg, (void *)(uintptr_t)(usg + i + 1));
	}
	thpool_wait(thpool);
	thpool_destroy(thpool);
	double rss = usg[0].rss, shr = usg[0].shr, cpu = usg[0].cpu;
	for (i = 0; i < kn; ++i)
	{
		rss += usg[i + 1].rss;
		shr = fmax(shr, usg[i + 1].shr);
		cpu += usg[i + 1].cpu;
	}
	free(usg);
	kv_destroy(kv);
	char buf[32];
	time_t _now = time(0);
	strftime(buf, sizeof(buf), "%x %X", localtime(&_now));
	if (strlen(cmds))
	{
		fprintf(fp, "%s\t%f\t%f\t%.3f\t%s\n", buf, fmax(0, rss), shr, cpu > nprocs() * 100 ? 100 : cpu, cmds);
		mn_t *mn = calloc(1, sizeof(mn_t));
		mn->ts = atot(buf);
		mn->rss = fmax(0, rss);
		mn->shr = shr;
		mn->cpu = cpu > nprocs() * 100 ? 100 : cpu;
		char *p = NULL, *q = strdup(cmds_ascii);
		if ((p = strchr(q, ';')))
		{
			if ((p = strchr(p + 1, ';')))
				*p = '\0';
			strcpy(mn->cmd, strchr(q, ';') + 1);
		}
		else
			strcpy(mn->cmd, q);
		free(q);
		mns[*n] = mn;
		if (*n + 1 == *m)
		{
			*m <<= 1;
			mns = realloc(mns, *m * sizeof(mn_t *));
		}
		(*n)++;
	}
	free(cmds);
	free(cmds_ascii);
}

int ndigit(const char *str)
{
	int i = 0, n = 0;
	while (str[i])
		n += (bool)isdigit(str[i++]);
	return n;
}

void usage()
{
	puts("\e[4mMonitor MEM & CPU of process by name, pid or cmds\e[0m");
	puts("Examples:");
	printf("  \e[1;31m%s\e[0;0m \e[35m<cmd>\e[0m\n", __progname);
	printf("  \e[1;31m%s\e[0;0m \e[35m<pid>\e[0m\n", __progname);
	printf("  \e[1;31m%s\e[0;0m \e[35m<cmd> <args> ...\e[0m\n", __progname);
}

// convert timestamp string to hms
void ttoa(time_t t)
{
	char a[9];
	struct tm *s = localtime(&t);
	strftime(a, sizeof(a), "%H:%M:%S", s);
	puts(a);
}

// convert timestamp string to time in seconds since 1900
time_t atot(const char *a)
{
	struct tm tm;
	strptime(a, "%m/%d/%y %H:%M:%S %Y", &tm);
	tm.tm_isdst = -1;
	time_t t = mktime(&tm);
	return t;
}

// convert seconds to time hms string
void stoa(const int sec, char **a)
{
	int h, m, s;
	h = (sec/3600); 
	m = (sec -(3600*h))/60;
	s = (sec -(3600*h)-(m*60));
	asprintf(a, "%02d:%02d:%02d", h, m, s);
}
