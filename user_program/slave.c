#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#define PAGE_SIZE 4096
#define BUF_SIZE 512
#define MAP_SIZE 8192 // 2 PAGE_SIZE
#define slave_IOCTL_CREATESOCK 0x12345677
#define slave_IOCTL_MMAP 0x12345678
#define slave_IOCTL_EXIT 0x12345679

struct mmap_ioctl_args {
    char *file_address;
    size_t length;
};

void help_message();

// ========================================

int main (int argc, char* argv[])
{
    char buf[BUF_SIZE];
    int dev_fd, file_fd;// the fd for the device and the fd for the input file
    size_t ret, file_size, total_file_size = 0, data_size = -1, offset;
    int n_files;
    char *file_name;
    struct mmap_ioctl_args mmap_args;
    struct timeval start;
    struct timeval end;
    //calulate the time between the device is opened and it is closed
    double trans_time;
    char *kernel_address, *file_address;

    // Deal with input parameters
    if (argc < 5)
    {
        help_message();
        return 1;
    }
    n_files = atoi(argv[1]);
    if (n_files <= 0 || n_files != argc - 4)
    {
        help_message();
        return 1;
    }
    const char *method = argv[argc - 2];
    const char *ip = argv[argc - 1];

    // ==============================

    gettimeofday(&start ,NULL);

    //should be O_RDWR for PROT_WRITE when mmap()
    if( (dev_fd = open("/dev/slave_device", O_RDWR)) < 0)
    {
        perror("failed to open /dev/slave_device\n");
        return 1;
    }

    // TODO:
    // Now, for each file I create a socket.
    // Maybe we need to consider how to transmit all files with single socket.

    for (int i = 2; n_files > 0; ++i, --n_files)
    {
        file_name = argv[i];
        file_size = 0;
        if( (file_fd = open (file_name, O_RDWR | O_CREAT | O_TRUNC, 0666)) < 0)
        {
            perror("failed to open input file\n");
            return 1;
        }

        // Connect to master in the device
        if(ioctl(dev_fd, slave_IOCTL_CREATESOCK, ip) == -1)
        {
            perror("ioctl create slave socket error\n");
            return 1;
        }

        switch(method[0])
        {
            case 'f'://fcntl : read()/write()
                // read from the the device
                while ((ret = read(dev_fd, buf, sizeof(buf))) > 0)
                {
                    write(file_fd, buf, ret); //write to the input file
                    total_file_size += ret;
                }
                break;

            case 'm': //mmap
                offset = 0; // Note that offset of mmap must be page aligned

                do
                {
                    if (ftruncate(file_fd, offset + MAP_SIZE) == -1)
                    {
                        perror("slave ftruncate error\n");
                        return 1;
                    }
                    file_address = mmap(NULL, MAP_SIZE,
                                        PROT_READ | PROT_WRITE, MAP_SHARED,
                                        file_fd, offset);
                    if (file_address == MAP_FAILED)
                    {
                        perror("slave file mmap error\n");
                        return 1;
                    }
                    mmap_args.file_address = file_address;
                    mmap_args.length = MAP_SIZE;

                    ret = ioctl(dev_fd, slave_IOCTL_MMAP, &mmap_args);
                    if (ret < 0)
                    {
                        perror("ioctl client mmap error\n");
                        return 1;
                    }

                    if (munmap(file_address, MAP_SIZE) != 0)
                    {
                        perror("master file munmap error\n");
                        return 1;
                    }

                    // MAP_SIZE is integer multiple of PAGE_SIZE,
                    // and ret is MAP_SIZE except at EOF,
                    // so it is safe to update offset by ret
                    offset += ret;
                    total_file_size += ret;

                } while (ret == MAP_SIZE);

                if (ftruncate(file_fd, offset) == -1)
                {
                    perror("slave ftruncate error\n");
                    return 1;
                }

                break;

            default:
                fprintf(stderr, "Invalid method : %s\n", method);
                return 1;
        }

        // end receiving data, close the connection
        if(ioctl(dev_fd, slave_IOCTL_EXIT) == -1)
        {
            perror("ioclt client exits error\n");
            return 1;
        }

        close(file_fd);
    }

    close(dev_fd);

    gettimeofday(&end, NULL);
    trans_time = (end.tv_sec - start.tv_sec)*1000 +
                 (end.tv_usec - start.tv_usec)*0.0001;
    printf("Transmission time: %lf ms, File size: %ld bytes\n",
            trans_time, total_file_size);

    return 0;
}

void help_message()
{
    printf("Usage: ./slave [NUM] [FILES] [fcntl | mmap] [IP_ADDRESS]\n");
}
