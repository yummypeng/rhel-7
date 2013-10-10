/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "sd-event.h"
#include "log.h"
#include "util.h"

static int prepare_handler(sd_event_source *s, void *userdata) {
        log_info("preparing %c", PTR_TO_INT(userdata));
        return 1;
}

static bool got_a, got_b, got_c;

static int io_handler(sd_event_source *s, int fd, uint32_t revents, void *userdata) {

        log_info("got IO on %c", PTR_TO_INT(userdata));

        if (userdata == INT_TO_PTR('a')) {
                assert_se(sd_event_source_set_mute(s, SD_EVENT_MUTED) >= 0);
                assert_se(!got_a);
                got_a = true;
        } else if (userdata == INT_TO_PTR('b')) {
                assert_se(!got_b);
                got_b = true;
        } else
                assert_not_reached("Yuck!");

        return 1;
}

static int child_handler(sd_event_source *s, const siginfo_t *si, void *userdata) {

        assert(s);
        assert(si);

        log_info("got child on %c", PTR_TO_INT(userdata));

        assert(userdata == INT_TO_PTR('f'));

        assert_se(sd_event_request_quit(sd_event_get(s)) >= 0);
        sd_event_source_unref(s);

        return 1;
}

static int signal_handler(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
        sd_event_source *p;
        sigset_t ss;
        pid_t pid;

        assert(s);
        assert(si);

        log_info("got signal on %c", PTR_TO_INT(userdata));

        assert(userdata == INT_TO_PTR('e'));

        assert_se(sigemptyset(&ss) >= 0);
        assert_se(sigaddset(&ss, SIGCHLD) >= 0);
        assert_se(sigprocmask(SIG_BLOCK, &ss, NULL) >= 0);

        pid = fork();
        assert_se(pid >= 0);

        if (pid == 0)
                _exit(0);

        assert_se(sd_event_add_child(sd_event_get(s), pid, WEXITED, child_handler, INT_TO_PTR('f'), &p) >= 0);
        assert_se(sd_event_source_set_mute(p, SD_EVENT_ONESHOT) >= 0);

        sd_event_source_unref(s);

        return 1;
}

static int defer_handler(sd_event_source *s, void *userdata) {
        sd_event_source *p;
        sigset_t ss;

        assert(s);

        log_info("got defer on %c", PTR_TO_INT(userdata));

        assert(userdata == INT_TO_PTR('d'));

        assert_se(sigemptyset(&ss) >= 0);
        assert_se(sigaddset(&ss, SIGUSR1) >= 0);
        assert_se(sigprocmask(SIG_BLOCK, &ss, NULL) >= 0);
        assert_se(sd_event_add_signal(sd_event_get(s), SIGUSR1, signal_handler, INT_TO_PTR('e'), &p) >= 0);
        assert_se(sd_event_source_set_mute(p, SD_EVENT_ONESHOT) >= 0);
        raise(SIGUSR1);

        sd_event_source_unref(s);

        return 1;
}

static bool do_quit = false;

static int time_handler(sd_event_source *s, uint64_t usec, void *userdata) {
        log_info("got timer on %c", PTR_TO_INT(userdata));

        if (userdata == INT_TO_PTR('c')) {

                if (do_quit) {
                        sd_event_source *p;

                        assert_se(sd_event_add_defer(sd_event_get(s), defer_handler, INT_TO_PTR('d'), &p) >= 0);
                        assert_se(sd_event_source_set_mute(p, SD_EVENT_ONESHOT) >= 0);
                } else {
                        assert(!got_c);
                        got_c = true;
                }
        } else
                assert_not_reached("Huh?");

        return 2;
}

int main(int argc, char *argv[]) {
        sd_event *e = NULL;
        sd_event_source *x = NULL, *y = NULL, *z = NULL;
        static const char ch = 'x';
        int a[2] = { -1, -1 }, b[2] = { -1, -1};

        assert_se(pipe(a) >= 0);
        assert_se(pipe(b) >= 0);

        assert_se(sd_event_new(&e) >= 0);

        got_a = false, got_b = false, got_c = false;

        assert_se(sd_event_add_io(e, a[0], EPOLLIN, io_handler, INT_TO_PTR('a'), &x) >= 0);
        assert_se(sd_event_add_io(e, b[0], EPOLLIN, io_handler, INT_TO_PTR('b'), &y) >= 0);
        assert_se(sd_event_add_monotonic(e, 0, time_handler, INT_TO_PTR('c'), &z) >= 0);

        assert_se(sd_event_source_set_priority(x, 99) >= 0);
        assert_se(sd_event_source_set_mute(y, SD_EVENT_ONESHOT) >= 0);
        assert_se(sd_event_source_set_prepare(x, prepare_handler) >= 0);
        assert_se(sd_event_source_set_priority(z, 50) >= 0);
        assert_se(sd_event_source_set_mute(z, SD_EVENT_ONESHOT) >= 0);
        assert_se(sd_event_source_set_prepare(z, prepare_handler) >= 0);

        assert_se(write(a[1], &ch, 1) >= 0);
        assert_se(write(b[1], &ch, 1) >= 0);

        assert_se(!got_a && !got_b && !got_c);

        assert_se(sd_event_run(e, (uint64_t) -1) >= 1);

        assert_se(!got_a && got_b && !got_c);

        assert_se(sd_event_run(e, (uint64_t) -1) >= 1);

        assert_se(!got_a && got_b && got_c);

        assert_se(sd_event_run(e, (uint64_t) -1) >= 1);

        assert_se(got_a && got_b && got_c);

        sd_event_source_unref(x);
        sd_event_source_unref(y);

        do_quit = true;
        assert_se(sd_event_source_set_time(z, now(CLOCK_MONOTONIC) + 200 * USEC_PER_MSEC) >= 0);
        assert_se(sd_event_source_set_mute(z, SD_EVENT_ONESHOT) >= 0);

        assert_se(sd_event_loop(e) >= 0);

        sd_event_source_unref(z);

        sd_event_unref(e);

        close_pipe(a);
        close_pipe(b);

        return 0;
}
