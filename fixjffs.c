#include <limits.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __GLIBC__		//musl doesn't have error.h
#include <error.h>
#endif	/* __GLIBC__ */
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/reboot.h>
#ifndef HND_ROUTER
#include <linux/compiler.h>
#endif
#include <mtd/mtd-user.h>
#include <stdint.h>
#ifdef RTCONFIG_REALTEK
#include <linux/fs.h>
#endif
#include <arpa/inet.h>

#include <trxhdr.h>
#include <bcmutils.h>

#ifdef RTCONFIG_BCMARM
#include <bcmendian.h>
#include <bcmnvram.h>
#include <shutils.h>
#endif
#include <shared.h>

#ifndef MNT_DETACH
#define MNT_DETACH	0x00000002
#endif

#define JFFS_NAME	"jffs2"

#ifdef HND_ROUTER
#define JFFS2_PARTITION	"misc2"
#else
#define JFFS2_PARTITION	"brcmnand"
#endif

#define JFFS2_MTD_NAME	JFFS2_PARTITION

static int mtd_open_old(const char *mtdname, mtd_info_t *mi)
{
	char path[256];
	int part;
	int size;
	int f;

	if (mtd_getinfo(mtdname, &part, &size)) {
		sprintf(path, "/dev/mtd%d", part);

		if ((f = open(path, O_RDWR|O_SYNC)) >= 0) {
			if ((mi) && ioctl(f, MEMGETINFO, mi) != 0) {
				close(f);
				return -2;
			}
			return f;
		}
	}
	return -1;
}

static int _unlock_erase(const char *mtdname, int erase)
{
	int mf;
	mtd_info_t mi;
	erase_info_t ei;
	int r, ret, skipbb;

	r = 0;
	skipbb = 0;
	if ((mf = mtd_open_old(mtdname, &mi)) >= 0) {
			r = 1;
			ei.length = mi.erasesize;
			for (ei.start = 0; ei.start < mi.size; ei.start += mi.erasesize) {
				printf("%sing 0x%x - 0x%x\n", erase ? "Eras" : "Unlock", ei.start, (ei.start + ei.length) - 1);
				fflush(stdout);
				if (!skipbb) {
					loff_t offset = ei.start;

					if ((ret = ioctl(mf, MEMGETBADBLOCK, &offset)) > 0) {
						printf("Skipping bad block at 0x%08x\n", ei.start);
						continue;
					} else if (ret < 0) {
						if (errno == EOPNOTSUPP) {
							skipbb = 1;	// nor
						} else {
							printf("MEMGETBADBLOCK\n");
							r = 0;
							break;
						}
					}
				}
				if (ioctl(mf, MEMUNLOCK, &ei) != 0) {
				}
				if (erase) {
					if (ioctl(mf, MEMERASE, &ei) != 0) {
						printf("MEMERASE\n");
						r = 0;
						break;
					}
				}
			}

			// checkme:
			char buf[2];
			read(mf, &buf, sizeof(buf));
			close(mf);
	}

	if (r) printf("\"%s\" successfully %s.\n", mtdname, erase ? "erased" : "unlocked");
	else printf("\nError %sing MTD\n", erase ? "eras" : "unlock");

	sleep(1);
	if (r)
		return 0;	//success
	return -1;		//erase fail
}

static int mtd_unlock(const char *mtdname)
{
	return _unlock_erase(mtdname, 0);
}

static int mtd_open(const char *mtd, int flags)
{
	FILE *fp;
	char dev[PATH_MAX];
	int i;

	if ((fp = fopen("/proc/mtd", "r"))) {
		while (fgets(dev, sizeof(dev), fp)) {
			if (sscanf(dev, "mtd%d:", &i) && strstr(dev, mtd)) {
				snprintf(dev, sizeof(dev), "/dev/mtd%d", i);
				fclose(fp);
				return open(dev, flags);
			}
		}
		fclose(fp);
	}

	return open(mtd, flags);
}

static int mtd_erase(const char *mtd)
{
	int mtd_fd;
	mtd_info_t mtd_info;
	erase_info_t erase_info;
	int skipbb = 0, ret;
	/* Open MTD device */
	if ((mtd_fd = mtd_open(mtd, O_RDWR)) < 0) {
		perror(mtd);
		return errno;
	}

	/* Get sector size */
	if (ioctl(mtd_fd, MEMGETINFO, &mtd_info) != 0) {
		perror(mtd);
		close(mtd_fd);
		return errno;
	}

	erase_info.length = mtd_info.erasesize;

	for (erase_info.start = 0;
	     erase_info.start < mtd_info.size;
	     erase_info.start += mtd_info.erasesize) {
		if (!skipbb) {
			loff_t offset = erase_info.start;
			if ((ret = ioctl(mtd_fd, MEMGETBADBLOCK, &offset)) > 0) {
				printf("Skipping bad block at 0x%08x\n", erase_info.start);
				continue;
			} else if (ret < 0) {
				if (errno == EOPNOTSUPP) {
					skipbb = 1;     // nor
				} else {
					perror(mtd);
					close(mtd_fd);
					return errno;
				}
			}
		}
		(void) ioctl(mtd_fd, MEMUNLOCK, &erase_info);
		if (ioctl(mtd_fd, MEMERASE, &erase_info) != 0) {
			perror(mtd);
			close(mtd_fd);
			return errno;
		}
	}

	close(mtd_fd);

	return 0;
}

unsigned int get_root_type(void)
{
	int model;

	model = get_model();

	switch(model) {
		case MODEL_RTAC3200:
		case MODEL_RTAC68U:
		case MODEL_RTAC88U:
		case MODEL_RTAC3100:
		case MODEL_RTAC5300:
			return 0x73717368;      /* squashfs */
		case MODEL_GTAC5300:
		case MODEL_RTAC86U:
		case MODEL_RTAX88U:
		case MODEL_GTAX11000:
		case MODEL_RTAX92U:
		case MODEL_RTAX95Q:
		case MODEL_RTAXE95Q:
		case MODEL_RTAX56_XD4:
		case MODEL_CTAX56_XD4:
		case MODEL_RTAX58U:
		case MODEL_RTAX55:
		case MODEL_RTAX56U:
		case MODEL_RPAX56:
		case MODEL_GTAXE11000:
			return 0x24051905;      /* ubifs */
	}
#ifdef HND_ROUTER
	return 0x24051905;      /* ubifs */
#else
	return 0x71736873;      /* squashfs */
#endif
}

static int check_mountpoint(char *mountpoint)
{
	FILE *procpt;
	char line[256], devname[48], mpname[48], system_type[10], mount_mode[128];
	int dummy1, dummy2;

	if ((procpt = fopen("/proc/mounts", "r")) != NULL)
	while (fgets(line, sizeof(line), procpt)) {
		memset(mpname, 0x0, sizeof(mpname));
		if (sscanf(line, "%s %s %s %s %d %d", devname, mpname, system_type, mount_mode, &dummy1, &dummy2) != 6)
			continue;

		if (!strcmp(mpname, mountpoint)){
			fclose(procpt);
			return 1;
		}
	}

	if (procpt)
		fclose(procpt);

	return 0;
}

static int check_in_rootfs(const char *mount_point, const char *msg_title, int format)
{
	struct statfs sf;

	if (!check_mountpoint((char *)mount_point)) return 1;

	if (statfs(mount_point, &sf) == 0) {
		if (sf.f_type != get_root_type()) {
			// already mounted
			notice_set(msg_title, format ? "Formatted" : "Loaded");
			return 0;
		}
	}
	return 1;
}

static void start_jffs2(void)
{
	int format = 0;
	char s[256];
	int size;
	int part;
	const char *p;
	int jffs2_fail = 0;

	if (!mtd_getinfo(JFFS2_PARTITION, &part, &size)) return;

	printf("start jffs2: %d, %d\n", part, size);

	if (mtd_erase(JFFS2_MTD_NAME)){
		printf("formatting\n");
		return;
	}

	format = 1;

	sprintf(s, "%d", size);
	p = nvram_get("jffs2_size");
	if ((p == NULL) || (strcmp(p, s) != 0)) {
		if (format) {
			nvram_set("jffs2_size", s);
			nvram_commit_x();
		}
		else if ((p != NULL) && (*p != 0)) {
			printf("verifying known size of\n");
			return;
		}
	}

	if (!check_in_rootfs("/jffs", "jffs", format))
		return;

	if (mtd_unlock(JFFS2_PARTITION)) {
		printf("unlocking\n");
		return;
	}

	modprobe(JFFS_NAME);
	sprintf(s, "/dev/mtdblock%d", part);
	if (mount(s, "/jffs", JFFS_NAME, MS_NOATIME, "") != 0) {
		if (mtd_erase(JFFS2_MTD_NAME)){
			jffs2_fail = 1;
			printf("formatting\n");
			return;
		}

		format = 1;
		if (mount(s, "/jffs", JFFS_NAME, MS_NOATIME, "") != 0) {
			printf("*** jffs2 2-nd mount error\n");
			//modprobe_r(JFFS_NAME);
			printf("mounting\n");
			jffs2_fail = 1;
			return;
		}
	}

	notice_set("jffs", format ? "Formatted" : "Loaded");
	if(jffs2_fail == 0)
		printf("jffs successfully mounted\n");
	else
		printf("jffs mount error\n");
}

int main(void)
{
	int model = get_model();

	switch(model) {
		case MODEL_RTAC3200:
		case MODEL_RTAC68U:
		case MODEL_RTAC88U:
		case MODEL_RTAC3100:
		case MODEL_RTAC5300:
		case MODEL_RTAC86U:
		case MODEL_GTAC5300:
		case MODEL_RTAX55:
		case MODEL_RTAX56U:
		case MODEL_RPAX56:
		case MODEL_RTAX58U:
		case MODEL_RTAX68U:
		case MODEL_RTAX86U:
		case MODEL_RTAX88U:
		case MODEL_GTAX11000:
		case MODEL_GTAXE11000:
		case MODEL_RTAX92U:
		case MODEL_RTAX95Q:
		case MODEL_RTAXE95Q:
		case MODEL_RTAX56_XD4:
		case MODEL_CTAX56_XD4:
			break;
		default:
			printf("Unsupported model\n");
			return 0;
	}
	if(!check_mountpoint("/jffs"))
		start_jffs2();
	return 0;
}
