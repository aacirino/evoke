#include "Executor.h"

#include "PendingCommand.h"
#include "Reporter.h"

#include <boost/process.hpp>
#include <cstring>
#include <functional>
#include <thread>

#ifdef LINUX
#include <unistd.h>
#endif

static std::promise<void> done;

class Process
{
public:
    Process(const std::string &filename, const std::string &cmd, std::function<void(Process *)> onComplete);

private:
    void run();
public:
    std::function<void(Process *)> onComplete;
    std::string filename;
    boost::process::ipstream pipe_stream;
    boost::process::child child;
    int errorcode = 0;
    std::string outbuffer;
    enum State
    {
        Running,
        Done
    };
    State state = Running;
};

Process::Process(const std::string &filename, const std::string &cmd, std::function<void(Process *)> onComplete) :
    onComplete(onComplete),
    filename(filename),
    child(cmd, (boost::process::std_out & boost::process::std_err) > pipe_stream)
{
    std::thread([this] { run(); }).detach();

}
void Process::run()
{
    std::string line;
    while(std::getline(pipe_stream, line))
        outbuffer += line + "\n";

#ifdef LINUX
    static long ticks = sysconf(_SC_CLK_TCK);

    // this fails if the process closes its stdout before finishing
    int fd = open(std::string("/proc/" + std::to_string(child.pid) + "/stat"), O_RDONLY);
    char buffer[4096];
    read(fd, buffer, 4096);
    close(fd);
    char* p = buffer;
    for (size_t n = 0; n < 15; n++) p = strchr(p, ' '); // skip 15
    long cutime, cstime;
    unsigned long vsize;
    // dummies
    long a, b, c, d;
    unsigned long long e;
    sscanf("%ld %ld %ld %ld %ld %ld %llu %lu", &cutime, &cstime, &a, &b, &c, &d, &e, &vsize);   
    double ctime = (cutime + cstime) / (double)ticks;
#else
    double ctime = 0;
    unsigned long vsize = 0;
#endif

    try {
        child.wait();
        errorcode = child.exit_code();
    } catch (const std::exception& e) {
        outbuffer = e.what();
        errorcode = -1;
    }
    state = Done;
    // The callback will cause this object to be destructed, so move out the callback before invoking it
    auto x = std::move(onComplete);
    x(this, ctime, vsize);
}

Executor::Executor(size_t jobcount, Reporter &reporter) 
: reporter(reporter)
{
    activeProcesses.resize(jobcount);
    reporter.SetConcurrencyCount(jobcount);
}

Executor::~Executor()
{
}

bool Executor::AllSuccess() {
    for (auto& command : commands) {
        if (command->errorcode) return false;
    }
    return true;
}

void Executor::Run(std::shared_ptr<PendingCommand> cmd)
{
    commands.push_back(cmd);
}

std::future<void> Executor::Mode(bool isDaemon)
{
    daemonMode = isDaemon;
    if (isDaemon) {
#ifndef _WIN32
    // TODO: Add the Windows side of signal handling
        void (*signalHandler)(int) = [](int signo){ done.set_value(); std::cout << " SIGNAL " << signo << "\n";};
        signal(SIGTERM, signalHandler);
        signal(SIGHUP, signalHandler);
        signal(SIGINT, signalHandler);
        signal(SIGQUIT, signalHandler);
        signal(SIGTSTP, signalHandler);
#endif
    }
    return done.get_future();
}

void Executor::NewGeneration() {
    generation++;
    commands.clear();
    reporter.ReportCommandQueue(commands);
}

void Executor::RunMoreCommands()
{
    reporter.ReportCommandQueue(commands);
    size_t n = 0;
    for(auto &c : commands)
    {
        while(n != activeProcesses.size() && activeProcesses[n])
            ++n;
        if(n == activeProcesses.size())
            break;
        // TODO: take into account its relative load
        if(c->CanRun())
        {
            c->state = PendingCommand::Running;
            for(auto &o : c->outputs)
            {
                fs::create_directories(o->path.parent_path());
            }
            reporter.SetRunningCommand(n, c);
            activeProcesses[n] = std::make_unique<Process>(c->outputs[0]->path.filename().string(), c->commandToRun, [this, n, c, generationWhenStarted = generation](Process *t) {
                std::lock_guard<std::mutex> l(m);
                auto self = std::move(activeProcesses[n]);
                if (generation == generationWhenStarted) {   // If the generation counter changed, then all our target pointers are stale. Don't talk to our command any more.
                  t->outbuffer.push_back(0);
                  c->SetResult(t->errorcode, t->outbuffer.data());
                  reporter.ReportCommand(n, c);
                } else {
                  reporter.ReportCommand(n, nullptr);
                }
                reporter.SetRunningCommand(n, nullptr);
                RunMoreCommands();
            });
        }
        else
        {
            ;
        }
    }

    for(auto &p : activeProcesses)
        if(p)
            return;

    if (!daemonMode)
        done.set_value();
}
