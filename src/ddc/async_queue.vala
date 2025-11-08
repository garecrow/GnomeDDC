using Gee;

namespace GnomeDDC.Ddc {
public delegate void AsyncWork(GLib.Cancellable? cancellable) throws Error;

public class AsyncQueue : Object {
    private LinkedList<AsyncWork> queue = new LinkedList<AsyncWork>();
    private bool running = false;
    private Cancellable cancellable = new Cancellable();

    public void enqueue(AsyncWork work) {
        queue.add(work);
        if (!running) {
            process_next.begin();
        }
    }

    private async void process_next() {
        if (queue.is_empty) {
            running = false;
            return;
        }

        running = true;
        var work = queue.poll_head();
        yield Task.run_in_thread<void>(() => {
            try {
                work(cancellable);
            } catch (Error e) {
                warning("Queued work failed: %s", e.message);
            }
        });

        Idle.add(() => {
            process_next.begin();
            return Source.REMOVE;
        });
    }

    public void cancel_all() {
        cancellable.cancel();
        queue.clear();
        cancellable = new Cancellable();
    }
}
}
