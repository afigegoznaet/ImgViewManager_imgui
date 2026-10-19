set pagination off
set print thread-events off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
handle SIGUSR1 stop noprint nopass
catch signal SIGUSR1
commands
silent
printf "\n=== Shutdown still pending: all thread stacks ===\n"
thread apply all bt
continue
end
python
import gdb
import os
import signal
import threading

finished = threading.Event()
gdb.events.exited.connect(lambda event: finished.set())

class ShutdownWatch(gdb.Breakpoint):
    def stop(self):
        pid = gdb.selected_inferior().pid
        gdb.write("\n=== Shutdown started ===\n")
        def watch():
            delay = 1
            while not finished.wait(delay):
                try:
                    os.kill(pid, signal.SIGUSR1)
                except ProcessLookupError:
                    return
                delay = 5
        threading.Thread(target=watch, daemon=True).start()
        self.enabled = False
        return False

ShutdownWatch("ShutdownDeadline::arm", internal=True)
end
run
thread apply all bt full
