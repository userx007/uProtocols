#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <liburing.h>
#include <vector>

class IoUringFileReader {
private:
    struct io_uring ring;
    std::vector<iovec> iovecs;
    int queue_depth;

public:
    IoUringFileReader(int depth = 32) : queue_depth(depth) {
        if (io_uring_queue_init(queue_depth, &ring, 0) < 0) {
            throw std::runtime_error("Failed to initialize io_uring");
        }
    }

    ~IoUringFileReader() {
        // Unregister buffers if registered
        if (!iovecs.empty()) {
            io_uring_unregister_buffers(&ring);
        }
        io_uring_queue_exit(&ring);
    }

    // Register fixed buffers for zero-copy I/O
    bool register_buffers(size_t buffer_count, size_t buffer_size) {
        iovecs.resize(buffer_count);
        
        for (size_t i = 0; i < buffer_count; i++) {
            void* buffer = aligned_alloc(4096, buffer_size);
            if (!buffer) {
                std::cerr << "Failed to allocate buffer" << std::endl;
                return false;
            }
            iovecs[i].iov_base = buffer;
            iovecs[i].iov_len = buffer_size;
        }

        if (io_uring_register_buffers(&ring, iovecs.data(), iovecs.size()) < 0) {
            std::cerr << "Failed to register buffers" << std::endl;
            return false;
        }

        std::cout << "Registered " << buffer_count << " buffers of " 
                  << buffer_size << " bytes each" << std::endl;
        return true;
    }

    // Read file using registered buffers
    ssize_t read_file_async(const char* filename) {
        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            perror("open");
            return -1;
        }

        // Get file size
        off_t file_size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        std::cout << "Reading file: " << filename 
                  << " (size: " << file_size << " bytes)" << std::endl;

        ssize_t total_read = 0;
        size_t buffer_size = iovecs[0].iov_len;
        int outstanding_reads = 0;
        off_t offset = 0;

        // Submit read requests
        while (offset < file_size || outstanding_reads > 0) {
            // Submit new reads up to queue depth
            while (offset < file_size && outstanding_reads < queue_depth) {
                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                if (!sqe) {
                    break;
                }

                int buf_index = outstanding_reads % iovecs.size();
                size_t read_size = std::min(buffer_size, 
                                           static_cast<size_t>(file_size - offset));

                // Use fixed buffer read for better performance
                io_uring_prep_read_fixed(sqe, fd, 
                                        iovecs[buf_index].iov_base,
                                        read_size, offset, buf_index);
                
                io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(offset));

                offset += read_size;
                outstanding_reads++;
            }

            // Submit all queued operations
            int submitted = io_uring_submit(&ring);
            if (submitted < 0) {
                std::cerr << "io_uring_submit failed" << std::endl;
                break;
            }

            // Wait for completions
            struct io_uring_cqe* cqe;
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                std::cerr << "io_uring_wait_cqe failed" << std::endl;
                break;
            }

            // Process completion
            off_t cqe_offset = reinterpret_cast<off_t>(
                io_uring_cqe_get_data(cqe));
            int bytes_read = cqe->res;

            if (bytes_read < 0) {
                std::cerr << "Read error at offset " << cqe_offset 
                         << ": " << strerror(-bytes_read) << std::endl;
            } else {
                total_read += bytes_read;
                std::cout << "Read " << bytes_read << " bytes at offset " 
                         << cqe_offset << std::endl;
            }

            io_uring_cqe_seen(&ring, cqe);
            outstanding_reads--;
        }

        close(fd);
        return total_read;
    }

    // Copy file using io_uring (demonstrates both read and write)
    bool copy_file(const char* src, const char* dst) {
        int src_fd = open(src, O_RDONLY);
        int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (src_fd < 0 || dst_fd < 0) {
            perror("open");
            if (src_fd >= 0) close(src_fd);
            if (dst_fd >= 0) close(dst_fd);
            return false;
        }

        off_t file_size = lseek(src_fd, 0, SEEK_END);
        lseek(src_fd, 0, SEEK_SET);

        std::cout << "Copying " << file_size << " bytes from " 
                  << src << " to " << dst << std::endl;

        const size_t chunk_size = 1024 * 1024; // 1MB chunks
        char* buffer = static_cast<char*>(aligned_alloc(4096, chunk_size));
        off_t offset = 0;

        while (offset < file_size) {
            size_t to_copy = std::min(chunk_size, 
                                     static_cast<size_t>(file_size - offset));

            // Read operation
            struct io_uring_sqe* sqe_r = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe_r, src_fd, buffer, to_copy, offset);
            io_uring_sqe_set_data(sqe_r, reinterpret_cast<void*>(1)); // READ marker

            io_uring_submit(&ring);

            // Wait for read to complete
            struct io_uring_cqe* cqe_r;
            io_uring_wait_cqe(&ring, &cqe_r);
            
            int bytes_read = cqe_r->res;
            io_uring_cqe_seen(&ring, cqe_r);

            if (bytes_read < 0) {
                std::cerr << "Read error: " << strerror(-bytes_read) << std::endl;
                break;
            }

            // Write operation
            struct io_uring_sqe* sqe_w = io_uring_get_sqe(&ring);
            io_uring_prep_write(sqe_w, dst_fd, buffer, bytes_read, offset);
            io_uring_sqe_set_data(sqe_w, reinterpret_cast<void*>(2)); // WRITE marker

            io_uring_submit(&ring);

            // Wait for write to complete
            struct io_uring_cqe* cqe_w;
            io_uring_wait_cqe(&ring, &cqe_w);
            
            int bytes_written = cqe_w->res;
            io_uring_cqe_seen(&ring, cqe_w);

            if (bytes_written < 0) {
                std::cerr << "Write error: " << strerror(-bytes_written) << std::endl;
                break;
            }

            offset += bytes_written;
            std::cout << "Progress: " << (offset * 100 / file_size) << "%\r" << std::flush;
        }

        std::cout << std::endl << "Copy completed" << std::endl;

        free(buffer);
        close(src_fd);
        close(dst_fd);
        return true;
    }
};

int main(int argc, char* argv[]) {
    try {
        IoUringFileReader reader(64);

        // Example 1: Register buffers and read a file
        if (reader.register_buffers(8, 4096)) {
            if (argc > 1) {
                reader.read_file_async(argv[1]);
            }
        }

        // Example 2: Copy a file
        if (argc > 2) {
            reader.copy_file(argv[1], argv[2]);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Compile: g++ -std=c++17 -o file_reader file_reader.cpp -luring
// Usage: ./file_reader input.txt [output.txt]