#ifndef PROGRESS_HH
#define PROGRESS_HH
#include <iostream>
#include <chrono>
#include <iomanip>

struct progress_t {
    std::chrono::time_point<std::chrono::system_clock> start;
    std::chrono::time_point<std::chrono::system_clock> current;
    double elapsed_seconds;
    uint64_t filesize;
    uint64_t sentsize;
    progress_t() :
        start(std::chrono::system_clock::now()),
        current(std::chrono::system_clock::now()),
        elapsed_seconds(0),
        filesize(0),
        sentsize(0) {}
};

void initialize_progress(progress_t& progress, uint64_t filesize) {
    progress.filesize = filesize;
    progress.start = std::chrono::system_clock::now();
}

void update_progress(progress_t& progress, uint64_t sentsize) {
    progress.sentsize = sentsize;
    progress.current = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = progress.current - progress.start;
    progress.elapsed_seconds = diff.count();
}

void update_progressbar(progress_t& progress) {
    float fraction = progress.sentsize/((float) progress.filesize);
    int barwidth = 70;
    std::cout <<"[";
    int pos = barwidth * fraction;
    for (int i = 0; i < barwidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(fraction * 100.0) << " % ";

    int hrs = int(progress.elapsed_seconds) / 3600;
    int mins = (int(progress.elapsed_seconds) % 3600) / 60;
    int secs = (int(progress.elapsed_seconds)) % 60;
    std::cout << std::setw(2) << std::setfill('0') << hrs << ":";
    std::cout << std::setw(2) << std::setfill('0') << mins << ":";
    std::cout << std::setw(2) << std::setfill('0') << secs << " ";

    int leftsize = progress.filesize - progress.sentsize;
    double rate = progress.sentsize / progress.elapsed_seconds;
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
    if (fraction >= 1.0) std::cout << std::endl;
}

#endif /* PROGRESS_HH */
