#include <iostream>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "command.h"

using std::vector;
using std::string;
using std::ostringstream;
using std::cout;
using std::endl;

command::command()
{
    readbuf = (char *)malloc(bufsize);
}

command &command::operator <<(int n)
{
    ostringstream s;
    s << n;
    args.push_back(s.str());
    return *this;
}

#ifdef COMMAND_BOOST_FILESYSTEM
command& command::operator<<(const boost::filesystem::path& p) {
    args.push_back(p.string());
    return *this;
}
#else
command& command::operator<<(const string& s) {
    args.push_back(s);
    return *this;
}
#endif

command &command::operator <<(const vector<string> &v)
{
    std::copy(v.begin(), v.end(), std::back_inserter(args));
    return *this;
}

command &command::operator <<(double d)
{
    ostringstream s;
    s << d;
    args.push_back(s.str());
    return *this;
}

void command::clear()
{
    args.clear();
    async = false;
}

int command::read_from(int fd, std::ostream *stream)
{
    int r = read(fd, readbuf, bufsize);
    if (r <= 0)
        return r;
    stream->write(readbuf, r);
    if (r == bufsize) {
        bufsize *= 2;
        free(readbuf);
        readbuf = (char *)malloc(bufsize);
    }
    return r;
}

int command::run() {
    if (args.empty())
        return -1;

    if (stdout_stream != nullptr)
        pipe(stdout_pipe);

    if (stderr_stream != nullptr)
        pipe(stderr_pipe);


    childpid = fork();
    if (childpid == 0) {
        char **argv = (char**)malloc((args.size() + 1) * sizeof(char*));
        for (uint i=0; i<args.size(); i++) {
            argv[i] = const_cast<char*>(args.at(i).c_str());
        }
        argv[args.size()] = nullptr;

        if (_silence_stdout || _silence_stderr) {
            int f = open("/dev/null", O_WRONLY);
            if (_silence_stdout)
                dup2(f, STDOUT_FILENO);
            if (_silence_stderr)
                dup2(f, STDERR_FILENO);
        }

        if (stdout_stream != nullptr) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[0]);
        }

        if (stderr_stream != nullptr) {
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[0]);
        }

        execvp(args.at(0).c_str(), argv);
        perror(args.at(0).c_str());
        exit(-1);
    }

    int child_out = -1;
    int child_err = -1;
    // parent won't be writing anything to the pipes
    // and keeping the write ends open will prevent
    // EOF from happening on the read ends
    if (stdout_stream != nullptr) {
        close(stdout_pipe[1]);
        child_out = stdout_pipe[0];
    }

    if (stderr_stream != nullptr) {
        child_err = stderr_pipe[0];
        close(stderr_pipe[1]);
    }

    if (async) {
        return 0;
    }

    // now we'll wait on both ends, using select
    while (child_err >=0 || child_out >= 0) {

        fd_set read_fds;
        FD_ZERO(&read_fds);
        int nfds = 0;
        if (child_out >= 0) {
            FD_SET(child_out, &read_fds);
            nfds = std::max(nfds, child_out);
        }
        if (child_err >= 0) {
            FD_SET(child_err, &read_fds);
            nfds = std::max(nfds, child_err);
        }
        nfds++;

        int r = select(nfds, &read_fds, nullptr, nullptr, nullptr);
        if (r < 0) {
            perror("select failed");
            break;
        }
        if (child_out >= 0 && FD_ISSET(child_out, &read_fds)) {
            int r = read_from(child_out, stdout_stream);
            if (r < 0) {
                perror("error reading from child stdout");
                break;
            }
            if (r == 0) {
                close(child_out);
                child_out = -1;
            }
        }
        if (child_err >= 0 && FD_ISSET(child_err, &read_fds)) {
            read_from(child_err, stderr_stream);
            if (r < 0) {
                perror("error reading from child stderr");
                break;
            }
            if (r == 0) {
                close(child_err);
                child_err = -1;
            }
        }
    }

    return this->wait();
}

void command::runbg()
{
    async = true;
    run();
}

bool command::isrunning()
{
    siginfo_t info;
    info.si_pid = 0;

    int r = waitid(P_PID, childpid, &info, WEXITED | WNOHANG | WNOWAIT);
    if (r == 0 && info.si_pid != 0)
        return true;

    switch (info.si_code) {
        case CLD_EXITED: return false;
        case CLD_KILLED: return false;
        case CLD_DUMPED: return false;
    }
    return true;
}

int command::wait()
{
    int status;
    ::wait(&status);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return WTERMSIG(status) + 127;
    return -1;
}

void command::silence_stdout()
{
    _silence_stdout = true;
    stdout_stream = nullptr;
}

void command::silence_stderr()
{
    stderr_stream = nullptr;
    _silence_stderr = true;
}

void command::silence()
{
    silence_stdout();
    silence_stderr();
}

void command::stdout(std::ostream &o)
{
    stdout_stream = &o;
    _silence_stdout = false;
}

void command::stderr(std::ostream &o)
{
    stderr_stream = &o;
    _silence_stderr = false;
}

std::ostream &operator<<(std::ostream &o, const command &cmd)
{
    for (auto& s: cmd.args) {
        o << s << " ";
    }
    return o;
}
