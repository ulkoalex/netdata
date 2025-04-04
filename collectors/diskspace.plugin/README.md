<!--
title: "Monitor disk (diskspace.plugin)"
description: "Monitor the disk usage space of mounted disks in real-time with the Netdata Agent, plus preconfigured alarms for disks at risk of filling up."
custom_edit_url: "https://github.com/netdata/netdata/edit/master/collectors/diskspace.plugin/README.md"
sidebar_label: "Disks"
learn_status: "Published"
learn_topic_type: "References"
learn_rel_path: "Integrations/Monitor/System metrics"
-->

# Monitor disk (diskspace.plugin)

This plugin monitors the disk space usage of mounted disks, under Linux. The plugin requires Netdata to have execute/search permissions on the mount point itself, as well as each component of the absolute path to the mount point.

Two charts are available for every mount:

-   Disk Space Usage
-   Disk Files (inodes) Usage

## configuration

Simple patterns can be used to exclude mounts from showed statistics based on path or filesystem. By default read-only mounts are not displayed. To display them `yes` should be set for a chart instead of `auto`.

By default, Netdata will enable monitoring metrics only when they are not zero. If they are constantly zero they are ignored. Metrics that will start having values, after Netdata is started, will be detected and charts will be automatically added to the dashboard (a refresh of the dashboard is needed for them to appear though). Set `yes` for a chart instead of `auto` to enable it permanently. You can also set the `enable zero metrics` option to `yes` in the `[global]` section which enables charts with zero metrics for all internal Netdata plugins.

```
[plugin:proc:diskspace]
    # remove charts of unmounted disks = yes
    # update every = 1
    # check for new mount points every = 15
    # exclude space metrics on paths = /proc/* /sys/* /var/run/user/* /run/user/* /snap/* /var/lib/docker/*
    # exclude space metrics on filesystems = *gvfs *gluster* *s3fs *ipfs *davfs2 *httpfs *sshfs *gdfs *moosefs fusectl autofs
    # space usage for all disks = auto
    # inodes usage for all disks = auto
```

Charts can be enabled/disabled for every mount separately:

```
[plugin:proc:diskspace:/]
    # space usage = auto
    # inodes usage = auto
```

> for disks performance monitoring, see the `proc` plugin, [here](https://github.com/netdata/netdata/blob/master/collectors/proc.plugin/README.md#monitoring-disks)


