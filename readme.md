# DSM Driver for UASP-supported USB storage devices

This is the USB Attached SCSI kernel module for Synology NASes.

In case you have external USB hard disks that support UASP connected to your Synology NAS, this driver will improve the read/write performance.

## What is USB Attached SCSI protocol?

**USB Attached SCSI (UAS)** or **USB Attached SCSI Protocol (UASP)** is a protocol used to move data to and from USB storage devices such as hard drives (HDDs), solid-state drives (SSDs), and thumb drives. UAS depends on the USB protocol and uses the standard SCSI command set. The use of UAS generally provides faster transfers compared to the older USB Mass Storage Bulk-Only Transport (BOT) drivers. 

With UAS, the read/write speed of mainly fine-grained files, so-called IOPS, is dramatically higher than with a conventional BOT. UAS also takes advantage of USB 3's ability to operate in a full duplex, which is advantageous when mixing both read and write operations on SSDs.

For simple sequential read/write access, there is little difference.

See [Wikipedia for a detailed explanation](https://en.wikipedia.org/wiki/USB_Attached_SCSI) and the actual benchmark described below.

## Supported NAS platform

* DSM 7.2
* apollolake based products
    * DS918+ (confirmed working)
    * DS620slim
    * DS1019+
    * DS718+
    * DS418play
    * DS218+

You can download drivers including other platforms from the [Release page](https://github.com/bb-qq/uas/releases) and determine a proper driver for your model from [this page](https://www.synology.com/en-global/knowledgebase/DSM/tutorial/Compatibility_Peripherals/What_kind_of_CPU_does_my_NAS_have), but you might encounter some issues with unconfirmed platforms. 

**Beta notice**: Currently this driver only supports apollolake and geminilake.

## Supported devices

This driver supports all UASP-enabled storage devices. However, **many UASP interoperability issues have been reported in Linux**. So the number of devices that actually work reliably may be limited.

### Devices confirmed

These devices have been proven to perform better with UASP and operate stably for longer periods of time.

* [Kuroutoshikou GW3.5AM-SU3G2P](https://amzn.to/3QwcaRH) (JMS580, Japan only)

### Devices expected to work

These devices support UASP and are equipped with relatively new chips. Theoretically, they work, but we are collecting reports on their stability.

* [UGREEN External Hard Drive Enclosure](https://amzn.to/46PeR6L) (ASM225CM)
* [ORICO USB 3.0 External Hard Drive Enclosure](https://amzn.to/3QdZFJ5) (JMS578)
* [StarTech.com USB 3.1 - 10Gbps - Hard Drive Adapter Cable (USB312SAT3)](https://amzn.to/49frucI) (ASM1153E)

[Other UASP-compatible devices can be found here](https://amzn.to/3MkiQjA).

## How to install

### Notice

Please note that this driver will not load when USB storage devices are mounted. To ensure the successful installation and execution of the driver, please [eject all USB storage devices from the control panel](https://kb.synology.com/en-us/DSM/help/DSM/AdminCenter/system_externaldevice_devicelist) before proceeding.

The reason is that this UAS driver replaces the stock USB storage driver. Therefore, the USB storage driver will be unloaded once before the UAS driver is loaded. To avoid unintended disconnection of the USB storage, the Run/Stop operation of the driver cannot be performed while the USB storage is mounted.

### Preparation

[Enable SSH](https://www.synology.com/en-us/knowledgebase/DSM/tutorial/General_Setup/How_to_login_to_DSM_with_root_permission_via_SSH_Telnet) and login your NAS.

### Installation

1. Go to "Package Center"
2. Press "Manual Install"
3. Choose a driver package downloaded from the [release page](https://github.com/bb-qq/uas/releases).
4. [DSM7] The installation will fail the first time. After that, run the following command from the SSH terminal:
   * `sudo install -m 4755 -o root -D /var/packages/uas/target/uas/spk_su /opt/sbin/spk_su`
5. [DSM7] Retry installation. 
   * You don't need the above DSM7-specific steps at the next time.

https://www.synology.com/en-us/knowledgebase/SRM/help/SRM/PkgManApp/install_buy

### How to check whether UAS is enabled

Run `lsusb -i` to verify that the *uas* driver is used instead of the *usb-storage* driver for the desired storage device.

The usb-storage (BOT) driver is used.
```
|__usb2          1d6b:0003:0404 09  3.00 5000MBit/s 0mA 1IF  (Linux 4.4.302+ xhci-hcd xHCI Host Controller 0000:00:15.0) hub
  |__2-2         152d:0580:7501 00  3.20 5000MBit/s 8mA 1IF  (Kuroutoshikou GW3.5AM-SU3G2P 37518000XXXX)
  2-2:1.0         (IF) 08:06:50 2EPs () usb-storage host17 (sdq)
```

The uas (UAS) driver is used.
```
|__usb2          1d6b:0003:0404 09  3.00 5000MBit/s 0mA 1IF  (Linux 4.4.302+ xhci-hcd xHCI Host Controller 0000:00:15.0) hub
  |__2-2         152d:0580:7501 00  3.20 5000MBit/s 8mA 1IF  (Kuroutoshikou GW3.5AM-SU3G2P 37518000XXXX)
  2-2:1.0         (IF) 08:06:62 4EPs () uas host25 (sdq)
```

Also, make sure that the device is connected via USB 3 by `3.20 5000MBit/s`.

## Known issues

### Automatic driver loading at system startup is useless.

USB storage is attached and mounted using the stock USB storage driver at system startup. The UAS driver is then loaded afterward.

Because of this order, the loading process of the UAS driver is skipped even if the UAS driver package is set to auto-run.

As a workaround, eject the USB storage and restart the driver package manually when the system is rebooted.

## Performance test

### Environment

* DS918+ (16 GB RAM, USB 3.2 Gen1x1 5Gbps)
* DSM 7.2-64570 Update 3
* [Kuroutoshikou GW3.5AM-SU3G2P](https://amzn.to/3QwcaRH) (JMicron JMS580 / USB 3.2 Gen2x1 10Gbps)
* [Seagate IronWolf 16TB ST16000VN001](https://amzn.to/45OQ2qi) (210 MB/s, 256 MB cache, 7200 rpm)
* [fio](https://github.com/axboe/fio) 3.29 installed by [Entware](https://github.com/Entware/Entware) opkg


### Scenario

For the parameters given to fio, I used those used to [measure the performance of persistent disks on Google Cloud Platform](https://cloud.google.com/compute/docs/disks/benchmarking-pd-performance).

<details>
  <summary>benchmark.sh</summary>

```
#!/bin/sh
set -eux

fio --name=write_throughput --directory=. --numjobs=8 \
--size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio \
--direct=1 --verify=0 --bs=1M --iodepth=64 --rw=write \
--group_reporting=1 --iodepth_batch_submit=64 \
--iodepth_batch_complete_max=64

fio --name=write_iops --directory=. --size=10G \
--time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 \
--verify=0 --bs=4K --iodepth=256 --rw=randwrite --group_reporting=1  \
--iodepth_batch_submit=256  --iodepth_batch_complete_max=256

fio --name=read_throughput --directory=. --numjobs=8 \
--size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio \
--direct=1 --verify=0 --bs=1M --iodepth=64 --rw=read \
--group_reporting=1 \
--iodepth_batch_submit=64 --iodepth_batch_complete_max=64
echo -----

fio --name=read_iops --directory=. --size=10G \
--time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 \
--verify=0 --bs=4K --iodepth=256 --rw=randread --group_reporting=1 \
--iodepth_batch_submit=256  --iodepth_batch_complete_max=256

fio --name=rw_iops --directory=. --size=10G \
--time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 \
--verify=0 --bs=4K --iodepth=256 --rw=randrw --group_reporting=1 \
--iodepth_batch_submit=256 --iodepth_batch_complete_max=256
```

</details>


### Result

#### Summary

* **write_throughput**: (g=0): rw=write, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
  * **BOT**: IOPS=182, BW=190MiB/s (200MB/s)(11.4GiB/61487msec); 0 zone resets
  * **UAS**: IOPS=187, BW=196MiB/s (206MB/s)(11.8GiB/61512msec); 0 zone resets
* **write_iops**: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
  * **BOT**: IOPS=727, BW=2926KiB/s (2997kB/s)(172MiB/60238msec); 0 zone resets
  * **UAS**: IOPS=747, BW=3005KiB/s (3078kB/s)(176MiB/60084msec); 0 zone resets
* **read_throughput**: (g=0): rw=read, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
  * **BOT**: IOPS=182, BW=190MiB/s (200MB/s)(11.4GiB/61115msec)
  * **UAS**: IOPS=178, BW=186MiB/s (195MB/s)(11.4GiB/62641msec)
* **read_iops**: (g=0): rw=randread, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
  * **BOT**: IOPS=192, BW=786KiB/s (805kB/s)(46.6MiB/60765msec)  
  * **UAS**: IOPS=608, BW=2452KiB/s (2510kB/s)(144MiB/60306msec)
* **rw_iops**: (g=0): rw=randrw, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
  * **BOT**:
    * read: IOPS=141, BW=574KiB/s (588kB/s)(33.9MiB/60439msec)
    * write: IOPS=140, BW=570KiB/s (584kB/s)(33.7MiB/60439msec); 0 zone resets
  * **UAS**: 
    * read: IOPS=290, BW=1169KiB/s (1197kB/s)(68.9MiB/60412msec)
    * write: IOPS=289, BW=1168KiB/s (1196kB/s)(68.9MiB/60412msec); 0 zone resets

#### Raw log

<details>
  <summary>BOT</summary>

```
+ fio --name=write_throughput --directory=. --numjobs=8 --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=1M --iodepth=64 --rw=write --group_reporting=1 --iodepth_batch_submit=64 --iodepth_batch_complete_max=64
write_throughput: (g=0): rw=write, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
...
fio-3.29
Starting 8 threads
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
write_throughput: Laying out IO file (1 file / 10240MiB)
Jobs: 6 (f=6): [W(6),_(2)][35.1%][w=82.0MiB/s][w=82 IOPS][eta 02m:00s]
write_throughput: (groupid=0, jobs=8): err= 0: pid=28746: Sat Oct 28 17:04:29 2023
  write: IOPS=182, BW=190MiB/s (200MB/s)(11.4GiB/61487msec); 0 zone resets
    slat (usec): min=207, max=3019.6k, avg=1903467.45, stdev=683391.87
    clat (usec): min=10, max=3112.3k, avg=511995.83, stdev=786204.39
     lat (msec): min=137, max=5893, avg=2380.43, stdev=749.29
    clat percentiles (usec):
     |  1.00th=[     12],  5.00th=[     14], 10.00th=[     16],
     | 20.00th=[     18], 30.00th=[     19], 40.00th=[     21],
     | 50.00th=[     26], 60.00th=[ 156238], 70.00th=[ 608175],
     | 80.00th=[1061159], 90.00th=[1837106], 95.00th=[2466251],
     | 99.00th=[2801796], 99.50th=[2868904], 99.90th=[3003122],
     | 99.95th=[3036677], 99.99th=[3036677]
   bw (  KiB/s): min=936101, max=1032192, per=100.00%, avg=994801.94, stdev=2734.42, samples=186
   iops        : min=  912, max= 1008, avg=971.13, stdev= 2.70, samples=186
  lat (usec)   : 20=37.01%, 50=16.08%, 250=0.51%, 500=1.55%, 750=0.48%
  lat (msec)   : 20=0.04%, 50=0.07%, 100=0.46%, 250=4.49%, 500=6.45%
  lat (msec)   : 750=6.19%, 1000=5.84%, 2000=12.88%, >=2000=8.21%
  cpu          : usr=0.26%, sys=0.22%, ctx=3555, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=8.0%, 8=20.6%, 16=22.9%, 32=44.6%, >=64=1.1%
     submit    : 0=0.0%, 4=7.2%, 8=7.2%, 16=14.4%, 32=20.3%, 64=50.8%, >=64=0.0%
     complete  : 0=0.0%, 4=2.0%, 8=0.5%, 16=1.0%, 32=0.5%, 64=96.0%, >=64=0.0%
     issued rwts: total=0,11193,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=64

Run status group 0 (all jobs):
  WRITE: bw=190MiB/s (200MB/s), 190MiB/s-190MiB/s (200MB/s-200MB/s), io=11.4GiB (12.3GB), run=61487-61487msec

Disk stats (read/write):
  sdq: ios=0/105756, merge=0/1378, ticks=0/10813223, in_queue=10819660, util=99.76%

+ fio --name=write_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randwrite --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
write_iops: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
write_iops: Laying out IO file (1 file / 10240MiB)
Jobs: 1 (f=1): [w(1)][100.0%][w=3335KiB/s][w=833 IOPS][eta 00m:00s]
write_iops: (groupid=0, jobs=1): err= 0: pid=29850: Sat Oct 28 17:05:32 2023
  write: IOPS=727, BW=2926KiB/s (2997kB/s)(172MiB/60238msec); 0 zone resets
    slat (usec): min=17, max=1100.1k, avg=148673.95, stdev=140363.23
    clat (usec): min=10, max=1769.8k, avg=193891.44, stdev=224609.75
     lat (msec): min=3, max=1937, avg=342.52, stdev=251.71
    clat percentiles (usec):
     |  1.00th=[     26],  5.00th=[     34], 10.00th=[     36],
     | 20.00th=[     44], 30.00th=[  38536], 40.00th=[ 117965],
     | 50.00th=[ 160433], 60.00th=[ 166724], 70.00th=[ 208667],
     | 80.00th=[ 325059], 90.00th=[ 484443], 95.00th=[ 658506],
     | 99.00th=[1035994], 99.50th=[1132463], 99.90th=[1384121],
     | 99.95th=[1484784], 99.99th=[1686111]
   bw (  KiB/s): min=  856, max= 5752, per=100.00%, avg=3049.65, stdev=852.07, samples=115
   iops        : min=  214, max= 1438, avg=762.25, stdev=213.04, samples=115
  lat (usec)   : 20=0.21%, 50=23.81%, 100=2.08%, 250=0.13%, 500=0.18%
  lat (usec)   : 750=0.09%, 1000=0.01%
  lat (msec)   : 2=0.02%, 4=0.17%, 10=0.51%, 20=0.44%, 50=6.37%
  lat (msec)   : 100=5.48%, 250=32.96%, 500=19.27%, 750=4.81%, 1000=2.58%
  lat (msec)   : 2000=1.17%
  cpu          : usr=0.24%, sys=1.21%, ctx=1552, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=99.9%
     submit    : 0=0.0%, 4=8.1%, 8=4.6%, 16=6.5%, 32=7.4%, 64=11.7%, >=64=61.7%
     complete  : 0=0.0%, 4=9.6%, 8=0.3%, 16=0.3%, 32=0.0%, 64=0.3%, >=64=89.6%
     issued rwts: total=0,43813,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
  WRITE: bw=2926KiB/s (2997kB/s), 2926KiB/s-2926KiB/s (2997kB/s-2997kB/s), io=172MiB (181MB), run=60238-60238msec

Disk stats (read/write):
  sdq: ios=0/46086, merge=0/3425, ticks=0/8986953, in_queue=9016725, util=99.90%

+ fio --name=read_throughput --directory=. --numjobs=8 --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=1M --iodepth=64 --rw=read --group_reporting=1 --iodepth_batch_submit=64 --iodepth_batch_complete_max=64
read_throughput: (g=0): rw=read, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
...
fio-3.29
Starting 8 threads
Jobs: 8 (f=8): [R(8)][14.2%][r=242MiB/s][r=242 IOPS][eta 06m:28s]
read_throughput: (groupid=0, jobs=8): err= 0: pid=5321: Sat Oct 28 17:12:57 2023
  read: IOPS=182, BW=190MiB/s (200MB/s)(11.4GiB/61115msec)
    slat (usec): min=291, max=2813.2k, avg=1915921.31, stdev=647683.73
    clat (usec): min=10, max=2882.9k, avg=534491.06, stdev=820259.87
     lat (msec): min=163, max=5360, avg=2416.82, stdev=716.99
    clat percentiles (usec):
     |  1.00th=[     12],  5.00th=[     14], 10.00th=[     15],
     | 20.00th=[     17], 30.00th=[     18], 40.00th=[     20],
     | 50.00th=[     23], 60.00th=[ 170918], 70.00th=[ 666895],
     | 80.00th=[1115685], 90.00th=[1988101], 95.00th=[2600469],
     | 99.00th=[2701132], 99.50th=[2734687], 99.90th=[2835350],
     | 99.95th=[2835350], 99.99th=[2868904]
   bw (  KiB/s): min=973368, max=1008366, per=100.00%, avg=990495.48, stdev=1324.00, samples=184
   iops        : min=  948, max=  984, avg=967.00, stdev= 1.32, samples=184
  lat (usec)   : 20=40.52%, 50=16.03%, 500=0.51%
  lat (msec)   : 50=0.06%, 100=0.19%, 250=4.10%, 500=6.95%, 750=3.78%
  lat (msec)   : 1000=4.50%, 2000=13.99%, >=2000=9.60%
  cpu          : usr=0.01%, sys=0.23%, ctx=3163, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=4.6%, 8=24.7%, 16=22.4%, 32=40.9%, >=64=4.0%
     submit    : 0=0.0%, 4=7.1%, 8=7.4%, 16=13.0%, 32=22.4%, 64=50.1%, >=64=0.0%
     complete  : 0=0.0%, 4=3.5%, 8=0.5%, 16=0.0%, 32=0.0%, 64=96.0%, >=64=0.0%
     issued rwts: total=11123,0,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=64

Run status group 0 (all jobs):
   READ: bw=190MiB/s (200MB/s), 190MiB/s-190MiB/s (200MB/s-200MB/s), io=11.4GiB (12.2GB), run=61115-61115msec

Disk stats (read/write):
  sdq: ios=104417/4, merge=0/1, ticks=9288673/45, in_queue=9295130, util=99.99%

+ fio --name=read_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randread --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
read_iops: (g=0): rw=randread, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
read_iops: Laying out IO file (1 file / 10240MiB)
Jobs: 1 (f=1): [r(1)][0.5%][r=1025KiB/s][r=256 IOPS][eta 03h:44m:39s]
read_iops: (groupid=0, jobs=1): err= 0: pid=7302: Sat Oct 28 17:14:50 2023
  read: IOPS=192, BW=786KiB/s (805kB/s)(46.6MiB/60765msec)
    slat (msec): min=279, max=743, avg=499.59, stdev=168.61
    clat (usec): min=16, max=2878.0k, avg=779571.39, stdev=616567.02
     lat (msec): min=279, max=3339, avg=1278.97, stdev=611.45
    clat percentiles (usec):
     |  1.00th=[     19],  5.00th=[     36], 10.00th=[     45],
     | 20.00th=[     67], 30.00th=[ 333448], 40.00th=[ 624952],
     | 50.00th=[ 666895], 60.00th=[ 935330], 70.00th=[1035994],
     | 80.00th=[1333789], 90.00th=[1652556], 95.00th=[1954546],
     | 99.00th=[2264925], 99.50th=[2332034], 99.90th=[2399142],
     | 99.95th=[2432697], 99.99th=[2768241]
   bw (  KiB/s): min=  768, max= 1034, per=100.00%, avg=1016.53, stdev=45.32, samples=93
   iops        : min=  192, max=  258, avg=253.98, stdev=11.32, samples=93
  lat (usec)   : 20=1.26%, 50=16.80%, 100=2.40%
  lat (msec)   : 500=16.18%, 750=23.07%, 1000=7.32%, 2000=30.78%, >=2000=3.29%
  cpu          : usr=0.06%, sys=0.12%, ctx=493, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=100.8%
     submit    : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=32.8%, >=64=67.2%
     complete  : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=100.0%
     issued rwts: total=11682,0,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
   READ: bw=786KiB/s (805kB/s), 786KiB/s-786KiB/s (805kB/s-805kB/s), io=46.6MiB (48.9MB), run=60765-60765msec

Disk stats (read/write):
  sdq: ios=12317/4, merge=1/1, ticks=9018565/163, in_queue=9023575, util=99.90%

+ fio --name=rw_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randrw --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
rw_iops: (g=0): rw=randrw, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
Jobs: 1 (f=1): [m(1)][0.7%][r=264KiB/s,w=248KiB/s][r=66,w=62 IOPS][eta 02h:34m:14s]
rw_iops: (groupid=0, jobs=1): err= 0: pid=29297: Sun Oct 29 12:00:26 2023
  read: IOPS=141, BW=574KiB/s (588kB/s)(33.9MiB/60439msec)
    slat (usec): min=46, max=1184.6k, avg=405646.76, stdev=229004.84
    clat (usec): min=13, max=2429.1k, avg=489209.95, stdev=482026.99
     lat (msec): min=87, max=2893, avg=894.68, stdev=516.64
    clat percentiles (usec):
     |  1.00th=[     26],  5.00th=[     36], 10.00th=[     37],
     | 20.00th=[     47], 30.00th=[  90702], 40.00th=[ 333448],
     | 50.00th=[ 396362], 60.00th=[ 455082], 70.00th=[ 683672],
     | 80.00th=[ 868221], 90.00th=[1216349], 95.00th=[1468007],
     | 99.00th=[1887437], 99.50th=[1971323], 99.90th=[2197816],
     | 99.95th=[2332034], 99.99th=[2432697]
   bw (  KiB/s): min=  320, max= 1162, per=100.00%, avg=650.14, stdev=239.00, samples=105
   iops        : min=   80, max=  290, avg=162.48, stdev=59.71, samples=105
  write: IOPS=140, BW=570KiB/s (584kB/s)(33.7MiB/60439msec); 0 zone resets
    slat (usec): min=56, max=1184.6k, avg=396779.43, stdev=219027.80
    clat (usec): min=12, max=2429.1k, avg=480735.80, stdev=482024.67
     lat (msec): min=71, max=2893, avg=877.38, stdev=518.27
    clat percentiles (usec):
     |  1.00th=[     23],  5.00th=[     36], 10.00th=[     37],
     | 20.00th=[     46], 30.00th=[  89654], 40.00th=[ 320865],
     | 50.00th=[ 387974], 60.00th=[ 446694], 70.00th=[ 666895],
     | 80.00th=[ 859833], 90.00th=[1199571], 95.00th=[1468007],
     | 99.00th=[1887437], 99.50th=[2004878], 99.90th=[2197816],
     | 99.95th=[2231370], 99.99th=[2432697]
   bw (  KiB/s): min=  408, max= 1136, per=100.00%, avg=648.52, stdev=233.11, samples=105
   iops        : min=  102, max=  284, avg=162.06, stdev=58.23, samples=105
  lat (usec)   : 20=0.32%, 50=24.88%, 100=3.35%, 250=0.45%
  lat (msec)   : 50=0.11%, 100=2.55%, 250=4.85%, 500=27.70%, 750=9.60%
  lat (msec)   : 1000=11.73%, 2000=14.82%, >=2000=0.41%
  cpu          : usr=0.09%, sys=0.22%, ctx=661, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=100.7%
     submit    : 0=0.0%, 4=0.5%, 8=11.6%, 16=9.5%, 32=3.0%, 64=8.0%, >=64=67.3%
     complete  : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=100.0%
     issued rwts: total=8545,8493,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
   READ: bw=574KiB/s (588kB/s), 574KiB/s-574KiB/s (588kB/s-588kB/s), io=33.9MiB (35.5MB), run=60439-60439msec
  WRITE: bw=570KiB/s (584kB/s), 570KiB/s-570KiB/s (584kB/s-584kB/s), io=33.7MiB (35.3MB), run=60439-60439msec

Disk stats (read/write):
  sdq: ios=8941/8957, merge=0/13, ticks=4548190/4406598, in_queue=9016782, util=99.90%

```
</details>

<details>
  <summary>UAS</summary>

````
+ fio --name=write_throughput --directory=. --numjobs=8 --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=1M --iodepth=64 --rw=write --group_reporting=1 --iodepth_batch_submit=64 --iodepth_batch_complete_max=64
write_throughput: (g=0): rw=write, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
...
fio-3.29
Starting 8 threads
Jobs: 4 (f=4): [_(3),W(1),_(1),W(3)][21.2%][w=266MiB/s][w=266 IOPS][eta 04m:01s]
write_throughput: (groupid=0, jobs=8): err= 0: pid=21679: Sun Oct 29 10:58:16 2023
  write: IOPS=187, BW=196MiB/s (206MB/s)(11.8GiB/61512msec); 0 zone resets
    slat (usec): min=124, max=3321.9k, avg=1524123.99, stdev=732852.58
    clat (usec): min=12, max=3324.5k, avg=824422.18, stdev=973069.16
     lat (msec): min=303, max=5113, avg=2342.02, stdev=984.54
    clat percentiles (usec):
     |  1.00th=[     15],  5.00th=[     16], 10.00th=[     17],
     | 20.00th=[     19], 30.00th=[     23], 40.00th=[   1188],
     | 50.00th=[ 350225], 60.00th=[ 692061], 70.00th=[1484784],
     | 80.00th=[1870660], 90.00th=[2231370], 95.00th=[2667578],
     | 99.00th=[3170894], 99.50th=[3204449], 99.90th=[3338666],
     | 99.95th=[3338666], 99.99th=[3338666]
   bw (  KiB/s): min=612992, max=999424, per=100.00%, avg=794826.45, stdev=11157.01, samples=243
   iops        : min=  598, max=  976, avg=775.95, stdev=10.91, samples=243
  lat (usec)   : 20=25.12%, 50=10.75%, 100=0.07%, 250=1.77%, 500=0.60%
  lat (usec)   : 750=0.59%, 1000=1.27%
  lat (msec)   : 2=5.52%, 4=2.58%, 250=0.38%, 500=6.24%, 750=5.91%
  lat (msec)   : 1000=1.79%, 2000=23.54%, >=2000=14.97%
  cpu          : usr=0.25%, sys=0.14%, ctx=1135, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=2.8%, 8=1.7%, 16=26.0%, 32=67.6%, >=64=2.8%
     submit    : 0=0.0%, 4=6.9%, 8=6.2%, 16=16.4%, 32=28.7%, 64=41.8%, >=64=0.0%
     complete  : 0=0.0%, 4=0.0%, 8=0.4%, 16=0.4%, 32=2.0%, 64=97.2%, >=64=0.0%
     issued rwts: total=0,11550,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=64

Run status group 0 (all jobs):
  WRITE: bw=196MiB/s (206MB/s), 196MiB/s-196MiB/s (206MB/s-206MB/s), io=11.8GiB (12.6GB), run=61512-61512msec

Disk stats (read/write):
  sdq: ios=0/24806, merge=0/24, ticks=0/9194404, in_queue=9221649, util=99.63%
+ fio --name=write_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randwrite --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
write_iops: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
Jobs: 1 (f=1): [w(1)][100.0%][w=3011KiB/s][w=752 IOPS][eta 00m:00s]
write_iops: (groupid=0, jobs=1): err= 0: pid=22682: Sun Oct 29 10:59:18 2023
  write: IOPS=747, BW=3005KiB/s (3078kB/s)(176MiB/60084msec); 0 zone resets
    slat (usec): min=57, max=1145.1k, avg=141624.61, stdev=142026.06
    clat (usec): min=7, max=1660.8k, avg=189729.16, stdev=214179.69
     lat (msec): min=4, max=1826, avg=331.83, stdev=249.23
    clat percentiles (usec):
     |  1.00th=[     28],  5.00th=[     35], 10.00th=[     36],
     | 20.00th=[  14091], 30.00th=[  82314], 40.00th=[ 152044],
     | 50.00th=[ 156238], 60.00th=[ 160433], 70.00th=[ 183501],
     | 80.00th=[ 304088], 90.00th=[ 350225], 95.00th=[ 566232],
     | 99.00th=[1115685], 99.50th=[1182794], 99.90th=[1384121],
     | 99.95th=[1484784], 99.99th=[1619002]
   bw (  KiB/s): min=  896, max= 6144, per=100.00%, avg=3358.26, stdev=941.82, samples=107
   iops        : min=  224, max= 1536, avg=839.46, stdev=235.49, samples=107
  lat (usec)   : 10=0.01%, 20=0.01%, 50=18.61%, 100=0.57%, 250=0.09%
  lat (msec)   : 4=0.13%, 10=0.14%, 20=1.09%, 50=5.36%, 100=4.89%
  lat (msec)   : 250=44.91%, 500=18.98%, 750=1.68%, 1000=1.73%, 2000=2.11%
  cpu          : usr=0.22%, sys=1.00%, ctx=1531, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=100.4%
     submit    : 0=0.0%, 4=5.5%, 8=2.8%, 16=2.5%, 32=9.5%, 64=13.0%, >=64=66.7%
     complete  : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=100.0%
     issued rwts: total=0,44889,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
  WRITE: bw=3005KiB/s (3078kB/s), 3005KiB/s-3005KiB/s (3078kB/s-3078kB/s), io=176MiB (185MB), run=60084-60084msec

Disk stats (read/write):
  sdq: ios=0/46020, merge=0/21, ticks=0/8699270, in_queue=8998243, util=99.90%
+ fio --name=read_throughput --directory=. --numjobs=8 --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=1M --iodepth=64 --rw=read --group_reporting=1 --iodepth_batch_submit=64 --iodepth_batch_complete_max=64
read_throughput: (g=0): rw=read, bs=(R) 1024KiB-1024KiB, (W) 1024KiB-1024KiB, (T) 1024KiB-1024KiB, ioengine=libaio, iodepth=64
...
fio-3.29
Starting 8 threads
Jobs: 7 (f=7): [R(3),_(1),R(4)][52.8%][r=177MiB/s][r=177 IOPS][eta 00m:58s]
read_throughput: (groupid=0, jobs=8): err= 0: pid=23660: Sun Oct 29 11:00:24 2023
  read: IOPS=178, BW=186MiB/s (195MB/s)(11.4GiB/62641msec)
    slat (usec): min=89, max=2904.8k, avg=1610604.80, stdev=640530.74
    clat (usec): min=13, max=2835.1k, avg=829009.40, stdev=959964.01
     lat (msec): min=658, max=5113, avg=2402.85, stdev=912.91
    clat percentiles (usec):
     |  1.00th=[     15],  5.00th=[     16], 10.00th=[     17],
     | 20.00th=[     18], 30.00th=[     20], 40.00th=[    562],
     | 50.00th=[   1045], 60.00th=[ 725615], 70.00th=[2088764],
     | 80.00th=[2122318], 90.00th=[2164261], 95.00th=[2164261],
     | 99.00th=[2231370], 99.50th=[2264925], 99.90th=[2298479],
     | 99.95th=[2298479], 99.99th=[2835350]
   bw (  KiB/s): min=620884, max=945086, per=100.00%, avg=787378.69, stdev=6960.73, samples=238
   iops        : min=  606, max=  922, avg=768.35, stdev= 6.79, samples=238
  lat (usec)   : 20=31.23%, 50=5.32%, 100=0.22%, 250=0.47%, 500=2.55%
  lat (usec)   : 750=5.94%, 1000=4.27%
  lat (msec)   : 2=1.87%, 500=0.39%, 750=10.91%, 1000=1.20%, 2000=4.63%
  lat (msec)   : >=2000=31.52%
  cpu          : usr=0.01%, sys=0.16%, ctx=833, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.6%, 8=0.0%, 16=31.5%, 32=63.7%, >=64=0.6%
     submit    : 0=0.0%, 4=3.9%, 8=11.1%, 16=11.8%, 32=28.3%, 64=44.8%, >=64=0.0%
     complete  : 0=0.0%, 4=0.0%, 8=0.4%, 16=1.2%, 32=1.6%, 64=96.8%, >=64=0.0%
     issued rwts: total=11159,0,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=64

Run status group 0 (all jobs):
   READ: bw=186MiB/s (195MB/s), 186MiB/s-186MiB/s (195MB/s-195MB/s), io=11.4GiB (12.2GB), run=62641-62641msec

Disk stats (read/write):
  sdq: ios=23281/3, merge=0/1, ticks=9275850/212, in_queue=9300474, util=99.93%
  
+ fio --name=read_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randread --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
read_iops: (g=0): rw=randread, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
Jobs: 1 (f=1): [r(1)][1.5%][r=2562KiB/s][r=640 IOPS][eta 01h:11m:20s]
read_iops: (groupid=0, jobs=1): err= 0: pid=24696: Sun Oct 29 11:01:26 2023
  read: IOPS=608, BW=2452KiB/s (2510kB/s)(144MiB/60306msec)
    slat (usec): min=33, max=290520, avg=177304.73, stdev=62677.13
    clat (usec): min=21, max=1080.7k, avg=234872.83, stdev=188632.97
     lat (msec): min=124, max=1363, avg=412.15, stdev=185.81
    clat percentiles (usec):
     |  1.00th=[     25],  5.00th=[     35], 10.00th=[     36],
     | 20.00th=[     46], 30.00th=[ 175113], 40.00th=[ 191890],
     | 50.00th=[ 202376], 60.00th=[ 221250], 70.00th=[ 283116],
     | 80.00th=[ 400557], 90.00th=[ 467665], 95.00th=[ 608175],
     | 99.00th=[ 792724], 99.50th=[ 826278], 99.90th=[ 910164],
     | 99.95th=[1010828], 99.99th=[1061159]
   bw (  KiB/s): min= 1603, max= 3080, per=99.85%, avg=2448.36, stdev=501.86, samples=120
   iops        : min=  398, max=  770, avg=612.07, stdev=125.50, samples=120
  lat (usec)   : 50=22.64%, 100=1.20%, 250=0.09%
  lat (msec)   : 100=0.04%, 250=43.60%, 500=24.16%, 750=7.34%, 1000=1.23%
  lat (msec)   : 2000=0.06%
  cpu          : usr=0.17%, sys=0.39%, ctx=1276, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=99.7%
     submit    : 0=0.0%, 4=0.5%, 8=0.0%, 16=0.0%, 32=6.1%, 64=19.7%, >=64=73.7%
     complete  : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=100.0%
     issued rwts: total=36706,0,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
   READ: bw=2452KiB/s (2510kB/s), 2452KiB/s-2452KiB/s (2510kB/s-2510kB/s), io=144MiB (151MB), run=60306-60306msec

Disk stats (read/write):
  sdq: ios=38091/0, merge=2/0, ticks=8879246/0, in_queue=8886403, util=99.90%

+ fio --name=rw_iops --directory=. --size=10G --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 --verify=0 --bs=4K --iodepth=256 --rw=randrw --group_reporting=1 --iodepth_batch_submit=256 --iodepth_batch_complete_max=256
rw_iops: (g=0): rw=randrw, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=libaio, iodepth=256
fio-3.29
Starting 1 thread
Jobs: 1 (f=1): [m(1)][1.4%][r=1773KiB/s,w=1809KiB/s][r=443,w=452 IOPS][eta 01h:14m:46s]
rw_iops: (groupid=0, jobs=1): err= 0: pid=15910: Sun Oct 29 15:14:46 2023
  read: IOPS=290, BW=1169KiB/s (1197kB/s)(68.9MiB/60412msec)
    slat (usec): min=18, max=999896, avg=186673.75, stdev=162647.74
    clat (usec): min=14, max=1723.5k, avg=280686.89, stdev=260561.18
     lat (msec): min=34, max=1906, avg=467.41, stdev=295.39
    clat percentiles (usec):
     |  1.00th=[     32],  5.00th=[     37], 10.00th=[     39],
     | 20.00th=[  92799], 30.00th=[ 168821], 40.00th=[ 191890],
     | 50.00th=[ 212861], 60.00th=[ 244319], 70.00th=[ 337642],
     | 80.00th=[ 413139], 90.00th=[ 599786], 95.00th=[ 876610],
     | 99.00th=[1199571], 99.50th=[1350566], 99.90th=[1518339],
     | 99.95th=[1619002], 99.99th=[1702888]
   bw (  KiB/s): min=  400, max= 2525, per=100.00%, avg=1285.28, stdev=413.35, samples=109
   iops        : min=  100, max=  631, avg=321.17, stdev=103.31, samples=109
  write: IOPS=289, BW=1168KiB/s (1196kB/s)(68.9MiB/60412msec); 0 zone resets
    slat (usec): min=47, max=999899, avg=184999.48, stdev=159017.06
    clat (usec): min=10, max=1550.6k, avg=209625.84, stdev=250546.54
     lat (msec): min=26, max=1793, avg=394.66, stdev=287.77
    clat percentiles (usec):
     |  1.00th=[     23],  5.00th=[     32], 10.00th=[     37],
     | 20.00th=[     39], 30.00th=[     70], 40.00th=[ 105382],
     | 50.00th=[ 179307], 60.00th=[ 198181], 70.00th=[ 231736],
     | 80.00th=[ 346031], 90.00th=[ 467665], 95.00th=[ 750781],
     | 99.00th=[1166017], 99.50th=[1283458], 99.90th=[1501561],
     | 99.95th=[1518339], 99.99th=[1551893]
   bw (  KiB/s): min=  384, max= 2605, per=100.00%, avg=1287.28, stdev=446.63, samples=109
   iops        : min=   96, max=  651, avg=321.67, stdev=111.62, samples=109
  lat (usec)   : 20=0.16%, 50=21.56%, 100=1.55%, 250=0.08%
  lat (msec)   : 20=0.14%, 50=1.11%, 100=5.29%, 250=38.07%, 500=21.22%
  lat (msec)   : 750=5.41%, 1000=2.88%, 2000=2.89%
  cpu          : usr=0.18%, sys=0.44%, ctx=1219, majf=0, minf=0
  IO depths    : 1=0.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=99.4%
     submit    : 0=0.0%, 4=0.7%, 8=0.0%, 16=4.4%, 32=12.6%, 64=14.3%, >=64=67.9%
     complete  : 0=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=100.0%
     issued rwts: total=17527,17506,0,0 short=0,0,0,0 dropped=0,0,0,0
     latency   : target=0, window=0, percentile=100.00%, depth=256

Run status group 0 (all jobs):
   READ: bw=1169KiB/s (1197kB/s), 1169KiB/s-1169KiB/s (1197kB/s-1197kB/s), io=68.9MiB (72.3MB), run=60412-60412msec
  WRITE: bw=1168KiB/s (1196kB/s), 1168KiB/s-1168KiB/s (1196kB/s-1196kB/s), io=68.9MiB (72.3MB), run=60412-60412msec

Disk stats (read/write):
  sdq: ios=18351/18401, merge=2/13, ticks=4993597/3839753, in_queue=8854692, util=99.90%
```
</details>
