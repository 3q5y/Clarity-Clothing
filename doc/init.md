Sample init scripts and service configuration for giantd
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/giantd.service:    systemd service unit configuration
    contrib/init/giantd.openrc:     OpenRC compatible SysV style init script
    contrib/init/giantd.openrcconf: OpenRC conf.d file
    contrib/init/giantd.conf:       Upstart service configuration file
    contrib/init/giantd.init:       CentOS compatible SysV style init script

1. Service User
---------------------------------

All three startup configurations assume the existence of a "giant" user
and group.  They must be created before attempting to use these scripts.

2. 