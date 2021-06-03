#include <stdio.h>
#include <libavformat/avformat.h>

static void logging(const char *fmt, ...);


int main(int argc, char **argv) {
    if (argc < 2) {
        printf("You need to specify a media file.\n");
        return -1;
    }

    logging("Initializing all the containers, codecs and protocols.");
    

    return 0;
}