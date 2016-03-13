#ifndef PROGRESS_HH
#define PROGRESS_HH
#include <sys/ioctl.h>
#include <iostream>
#include <chrono>
#include <iomanip>

struct progress_t {

    std::chrono::time_point<std::chrono::system_clock> start;

    std::chrono::time_point<std::chrono::system_clock> current;

    double elapsed_seconds;

    uint64_t workSize;

    uint64_t completed;

    bool debug;

    progress_t(uint64_t workSize, bool debug = false) :
        start(std::chrono::system_clock::now()),
        current(std::chrono::system_clock::now()),
        elapsed_seconds(0),
        workSize(workSize),
        completed(0),
        debug(debug)
    {}

    /**
     * Updates the status of the progress bar and displays it.
     *
     * \param completed
     *      The unit of works that are currently completed.
     */
    void update(uint64_t completed) {
        this->completed = completed;
        current = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = current - start;
        elapsed_seconds = diff.count();
        show();
    }

    /**
     * Displays the current status of the progress bar.
     */
    void show() const {
        if (debug) return;

        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

        float fraction = completed * 1.0f / workSize;
        if (w.ws_col >= 35) {
            int barwidth = (w.ws_col - 30 < 70)? w.ws_col - 30 : 70;
            std::cout <<"[";
            int pos = barwidth * fraction;
            for (int i = 0; i < barwidth; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] "; 
        }

        int percentage = static_cast<int>(fraction * 100);
        assert(percentage <= 100);
        std::cout << percentage << " % ";

        int hrs = int(elapsed_seconds) / 3600;
        int mins = (int(elapsed_seconds) % 3600) / 60;
        int secs = (int(elapsed_seconds)) % 60;
        std::cout << std::setw(2) << std::setfill('0') << hrs << ":";
        std::cout << std::setw(2) << std::setfill('0') << mins << ":";
        std::cout << std::setw(2) << std::setfill('0') << secs << " ";

        int leftsize = workSize - completed;
        assert(leftsize >= 0);
        double rate = completed / elapsed_seconds;
        if (rate > 0) {
            int remaining_seconds = (int) (leftsize / rate);

            hrs = remaining_seconds / 3600;
            mins = (remaining_seconds % 3600) / 60;
            secs = remaining_seconds % 60;
            std::cout << "ETA: ";
            std::cout << std::setw(2) << std::setfill('0') << hrs << ":";
            std::cout << std::setw(2) << std::setfill('0') << mins << ":";
            std::cout << std::setw(2) << std::setfill('0') << secs;
        }
        else std::cout << "ETA: --:--:--";

        std::cout << "\r";
        std::cout.flush();
        if (leftsize == 0) std::cout << std::endl;
    }

};


#endif /* PROGRESS_HH */
