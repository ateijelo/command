#ifndef COMMAND_H
#define COMMAND_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>

#if __has_include(<boost/filesystem.hpp>)
#include <boost/filesystem.hpp>
#define COMMAND_BOOST_FILESYSTEM
#endif

class command {
    public:
        command();
        command& operator <<(int n);
#ifdef COMMAND_BOOST_FILESYSTEM
        command& operator <<(const boost::filesystem::path& p);
#else
        command& operator <<(const std::string& s);
#endif
        command& operator <<(const std::vector<std::string>& v);
        command& operator <<(double d);
        void clear();
        int run();
        void runbg();
        bool isrunning();
        int wait();

        void silence_stdout();
        void silence_stderr();
        void silence();
        void stdout(std::ostream& o);
        void stderr(std::ostream& o);

    private:
        std::vector<std::string> args;

        pid_t childpid;

        int stdout_pipe[2];
        int stderr_pipe[2];

        bool _silence_stdout = false;
        bool _silence_stderr = false;

        std::ostream* stdout_stream = nullptr;
        std::ostream* stderr_stream = nullptr;

        friend std::ostream& operator<<(std::ostream& o, const command& cmd);

        char *readbuf;
        int bufsize = 1024;
        int async = false;
        int read_from(int fd, std::ostream *stream);
};

std::ostream& operator<<(std::ostream& o, const command& cmd);

#endif // COMMAND_H
