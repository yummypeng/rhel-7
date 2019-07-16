#!/bin/bash
set -e
TEST_DESCRIPTION="plugged -> dead -> plugged issue #11997"
TEST_NO_NSPAWN=1

. $TEST_BASE_DIR/test-functions
QEMU_TIMEOUT=300

test_setup() {
    create_empty_image
    mkdir -p $TESTDIR/root
    mount ${LOOPDEV}p1 $TESTDIR/root

    (
        LOG_LEVEL=5
        eval $(udevadm info --export --query=env --name=${LOOPDEV}p2)

        setup_basic_environment

        # mask some services that we do not want to run in these tests
        ln -s /dev/null $initdir/etc/systemd/system/systemd-hwdb-update.service
        ln -s /dev/null $initdir/etc/systemd/system/systemd-journal-catalog-update.service
        ln -s /dev/null $initdir/etc/systemd/system/systemd-networkd.service
        ln -s /dev/null $initdir/etc/systemd/system/systemd-networkd.socket
        ln -s /dev/null $initdir/etc/systemd/system/systemd-resolved.service

        # setup the testsuite service
        cat >$initdir/etc/systemd/system/testsuite.service <<EOF
[Unit]
Description=Testsuite service

[Service]
ExecStart=/bin/bash -x /testsuite.sh
Type=oneshot
StandardOutput=tty
StandardError=tty
EOF
        cp testsuite.sh $initdir/

        setup_testsuite
    )

    ddebug "umount $TESTDIR/root"
    umount $TESTDIR/root
}

do_test "$@"