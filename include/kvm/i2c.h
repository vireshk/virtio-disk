#include "kvm/read-write.h"
#include "kvm/util.h"

#include <linux/types.h>
#include <linux/fs.h>	/* for BLKGETSIZE64 */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef CONFIG_HAS_AIO
#include <libaio.h>
#endif

#define MAX_I2C 1

struct i2c_params {
    u32 addr;
    u8 irq;

    u32 call;
    u32 kick;
    unsigned long long mem;
    unsigned long long desc;
    unsigned long long used;
    unsigned long long avail;
};
