/* DISK ON FILE(disof) BLOCK DEVICE DRIVER Driver */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h> // For at least, struct block_device_operations
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>


#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

#define SECTOR_SIZE 512
#define MBR_SIZE SECTOR_SIZE
#define MBR_DISK_SIGNATURE_OFFSET 440
#define MBR_DISK_SIGNATURE_SIZE 4
#define PARTITION_TABLE_OFFSET 446
#define PARTITION_ENTRY_SIZE 16 // sizeof(PartEntry)
#define PARTITION_TABLE_SIZE 64 // sizeof(PartTable)
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE_SIZE 2
#define MBR_SIGNATURE 0xAA55
#define BR_SIZE SECTOR_SIZE
#define BR_SIGNATURE_OFFSET 510
#define BR_SIGNATURE_SIZE 2
#define BR_SIGNATURE 0xAA55

#define disof_FIRST_MINOR 0
#define disof_MINOR_CNT 8

#define disof_SECTOR_SIZE 512
#define disof_DEVICE_SIZE 1024 /* sectors */
/* So, total device size = 1024 * 512 bytes = 512 KiB ( Since 1 sector = 512 Bytes) */

/* Array where the disk stores its data */
static u8 *dev_data;

/* Major number to be reserved */
static u_int disof_major = 0;  






//--------------------Partition Table Entry struture---------------------------
//Struture that stores the entried of the partition table in the DOS partition
//table format.(16 Bytes)
//-----------------------------------------------------------------------------
typedef struct
{
	unsigned char boot_type; // 0x00 - Inactive; 0x80 - Active (Bootable)
	unsigned char start_head;
	unsigned char start_sec:6;
	unsigned char start_cyl_hi:2;
	unsigned char start_cyl;
	unsigned char part_type;
	unsigned char end_head;
	unsigned char end_sec:6;
	unsigned char end_cyl_hi:2;
	unsigned char end_cyl;
	unsigned int abs_start_sec;
	unsigned int sec_in_part;
} PartEntry;

// The are four partitions in the partition table in MBR 
typedef PartEntry PartTable[4]; 

//--------------------Partitions disof1 and disof2---------------------------
//Defining the disof1 and disof2 partition table entries
//-----------------------------------------------------------------------------
static PartTable def_part_table =
{
	{
		boot_type: 0x00,
		start_head: 0x00,
		start_sec: 0x2,
		start_cyl: 0x00,
		part_type: 0x83,
		end_head: 0x00,
		end_sec: 0x20,
		end_cyl: 0x09,
		abs_start_sec: 0x00000001,
		sec_in_part: 0x0000013F
	},
	{
		boot_type: 0x00,
		start_head: 0x00,
		start_sec: 0x1,
		start_cyl: 0x14,
		part_type: 0x83,
		end_head: 0x00,
		end_sec: 0x20,
		end_cyl: 0x1F,
		abs_start_sec: 0x00000280,
		sec_in_part: 0x00000180
	},
         
};

//function to copy mbr block
static void copy_mbr(u8 *disk)
{
	memset(disk, 0x0, MBR_SIZE);
	*(unsigned long *)(disk + MBR_DISK_SIGNATURE_OFFSET) = 0x36E5756D;
	memcpy(disk + PARTITION_TABLE_OFFSET, &def_part_table, PARTITION_TABLE_SIZE);
	*(unsigned short *)(disk + MBR_SIGNATURE_OFFSET) = MBR_SIGNATURE;


	loff_t pos=0;
	//Declaring file pointer 
	struct file *disof_file;

	//Opening file 
	disof_file = filp_open("/etc/user.txt",  O_RDWR, 777);

       kernel_write(disof_file,&def_part_table,PARTITION_TABLE_SIZE, &pos);

       //close the file
	filp_close(disof_file,NULL); 
        
}


//function used to intialzie the disk on file 
int disofdevice_init(void)
{
	dev_data = vmalloc(disof_DEVICE_SIZE * disof_SECTOR_SIZE);
	if (dev_data == NULL)
		return -ENOMEM;
	/* Setup its partition table */
	copy_mbr(dev_data);
	return disof_DEVICE_SIZE;
}

//function to clear the device

void disofdevice_cleanup(void)
{
	vfree(dev_data);
}
// main function to write to file when called in the device transfer function

void disofdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(dev_data + sector_off * disof_SECTOR_SIZE, buffer,
		sectors * disof_SECTOR_SIZE);

	 
	struct file *disof_file;

	//Opening file 
	disof_file = filp_open("/etc/user.txt",  O_RDWR, 777);

 	kernel_write(disof_file,buffer,sectors * disof_SECTOR_SIZE, 	sector_off*disof_SECTOR_SIZE);

	//close the file
	filp_close(disof_file,NULL); 
}

// main function to write to file when called in the device transfer function

void disofdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(buffer, dev_data + sector_off * disof_SECTOR_SIZE,
		sectors * disof_SECTOR_SIZE);

	//Declaring file pointer 
	struct file *disof_file;	

	//open the file
        disof_file = filp_open("/etc/user.txt",  O_RDWR, 777);

	kernel_read(disof_file, sector_off*disof_SECTOR_SIZE,buffer, sectors * disof_SECTOR_SIZE);

 	//close the file
	filp_close(disof_file,NULL);
        
}


// internal kernel function to represent device 
static struct disof_device
{
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For exclusive access to our request queue */
	spinlock_t lock;
	/* Our request queue */
	struct request_queue *disof_queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *disof_disk;
} disof_dev;


//THis fucntion will be called in order to open the disk on file

static int disof_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	printk(KERN_INFO "disof: Device is opened\n");
	printk(KERN_INFO "disof: Inode number is %d\n", unit);

	if (unit > disof_MINOR_CNT)
		return -ENODEV;
	return 0;
}

//THis fucntion will be called in order to close the disk on file

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
static int disof_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "disof: Device is closed\n");
	return 0;
}
#else
static void disof_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "disof: Device is closed\n");
}
#endif
// function called when the actual device transfer takes place called from init module
static int disof_transfer(struct request *req)
{

	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

	struct bio_vec bv;
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0;

	sector_offset = 0;
    rq_for_each_segment(bv, req, iter)
    {
        buffer = page_address(bv.bv_page) + bv.bv_offset;
        if (bv.bv_len % SECTOR_SIZE != 0)
        {
            printk(  "rb: error : "
                "bio size (%d) is not a multiple of SECTOR_SIZE (%d).\n"
                "data lost.\n",
                bv.bv_len, SECTOR_SIZE);
            ret = -EIO;
        }
        sectors = bv.bv_len / SECTOR_SIZE;
        printk(KERN_DEBUG "rb: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
            sector_offset, buffer, sectors);
		if (dir == WRITE) /* Write to the device */
		{
			disofdevice_write(start_sector + sector_offset, buffer, sectors);
		}
		else /* Read from the device */
		{
			disofdevice_read(start_sector + sector_offset, buffer, sectors);
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
		printk( "disof: sector mismatch");
		ret = -EIO;
	}

	return ret;
}
//function of request which will have the details about request queue-
static void disof_request(struct request_queue *q)
{
	struct request *req;
	int ret;

	/* Gets the current request from the dispatch queue */
	while ((req = blk_fetch_request(q)) != NULL)
	{
		ret = disof_transfer(req);
		__blk_end_request_all(req, ret);
	}
}

//block device operation structures like file oeprqations 
static struct block_device_operations disof_fops =
{
	.owner = THIS_MODULE,
	.open = disof_open,
	.release = disof_close,
	
};
	
//init function
static int __init disof_init(void)
{

	int ret;

	/* Set up our disof Device */
	if ((ret = disofdevice_init()) < 0)
	{
		return ret;
	}
	disof_dev.size = ret;

	/* Get Registered */
	disof_major = register_blkdev(disof_major, "dof");
	if (disof_major <= 0)
	{
		printk(  "disof: can't get Major Number\n");
		disofdevice_cleanup();
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&disof_dev.lock);
	disof_dev.disof_queue = blk_init_queue(disof_request, &disof_dev.lock);
	if (disof_dev.disof_queue == NULL)
	{
		printk(  "disof: blk_init_queue failure\n");
		unregister_blkdev(disof_major, "dof");
		disofdevice_cleanup();
		return -ENOMEM;
	}
	
	/*
	 * Add the gendisk structure
	 * By using this memory allocation is involved, 
	 * the minor number we need to pass bcz the device 
	 * will support this much partitions 
	 */
	disof_dev.disof_disk = alloc_disk(disof_MINOR_CNT);
	if (!disof_dev.disof_disk)
	{
		printk(  "disof: alloc_disk failure\n");
		blk_cleanup_queue(disof_dev.disof_queue);
		unregister_blkdev(disof_major, "dof");
		disofdevice_cleanup();
		return -ENOMEM;
	}

 	/* Setting the major number */
	disof_dev.disof_disk->major = disof_major;
  	/* Setting the first mior number */
	disof_dev.disof_disk->first_minor = disof_FIRST_MINOR;
 	/* Initializing the device operations */
	disof_dev.disof_disk->fops = &disof_fops;
 	/* Driver-specific own internal data */
	disof_dev.disof_disk->private_data = &disof_dev;
	disof_dev.disof_disk->queue = disof_dev.disof_queue;
	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//disof_dev.disof_disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(disof_dev.disof_disk->disk_name, "dof");
	/* Setting the capacity of the device in its gendisk structure */
	set_capacity(disof_dev.disof_disk, disof_dev.size);

	/* Adding the disk to the system */
	add_disk(disof_dev.disof_disk);
	/* Now the disk is "live" */
	printk(KERN_INFO "disof: disof Block driver initialised (%d sectors; %d bytes)\n",
		disof_dev.size, disof_dev.size * disof_SECTOR_SIZE);

	return 0;
}

//cleanup function
static void __exit disof_cleanup(void)
{       


	del_gendisk(disof_dev.disof_disk);
	put_disk(disof_dev.disof_disk);
	blk_cleanup_queue(disof_dev.disof_queue);
	unregister_blkdev(disof_major, "dof");
	disofdevice_cleanup();
}

//----------Initialisation------ 
module_init(disof_init);

//-------Exiting-------
module_exit(disof_cleanup);

//Module Information 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arpit Shukla");
MODULE_DESCRIPTION("Assigment 2: Block Device Driver ");


//----------------------------------END-------------------------------------

