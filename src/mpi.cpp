#include <chrono>
#include <iostream>
#include <graphic/graphic.hpp>
#include <imgui_impl_sdl.h>
#include <vector>
#include <complex>
#include <mpi.h>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>
int dynamic_flag = 0;
int gui_flag = 0;
struct Parameter {
    int size;
    int scale;
    double x_center;
    double y_center;
    int k_value;
    int proc;
};

struct Row {
    int index;
    int content[1];
};

struct Square {
    std::vector<int> buffer;
    size_t length;

    explicit Square(size_t length) : buffer(length), length(length * length) {}

    void resize(size_t new_length) {
        buffer.assign(new_length * new_length, false);
        length = new_length;
    }

    auto& operator[](std::pair<size_t, size_t> pos) {
        return buffer[pos.second * length + pos.first];
    }
};

static inline int getLength(const int &size, const int &proc, const int &rank){
    if(size<(proc-1)) return (rank-1)<size;
    return (size-rank+1)/proc + ((size-rank+1)%proc > 0); // ceil funct
}


void calculate(Square &buffer, int size, int scale, double x_center, double y_center, int k_value, int proc, Parameter* para) {
    if(proc==1) { // sequential version
        double cx = static_cast<double>(size) / 2 + x_center;
        double cy = static_cast<double>(size) / 2 + y_center;
        double zoom_factor = static_cast<double>(size) / 4 * scale;
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                double x = (static_cast<double>(j) - cx) / zoom_factor;
                double y = (static_cast<double>(i) - cy) / zoom_factor;
                std::complex<double> z{0, 0};
                std::complex<double> c{x, y};
                int k = 0;
                do {
                    z = z * z + c;
                    k++;
                } while (norm(z) < 2.0 && k < k_value);
                buffer[{i, j}] = k;
            }
        }
        return;
    }
    para->size = size;
    para->x_center = x_center;
    para->y_center = y_center;
    para->scale = scale;
    para->proc = proc;
    para->k_value = k_value;
    MPI_Bcast(para, sizeof(struct Parameter), MPI_BYTE, 0, MPI_COMM_WORLD);
    if(!dynamic_flag){ //static version
        int* recvcounts = static_cast <int*>(malloc(proc*sizeof(int)));
        int* displs = static_cast <int*>(malloc(proc*sizeof(int)));
        int* results = static_cast <int*>(malloc(size*size*sizeof(int)));
        recvcounts[0] = 0;
        int count = 0;
        for(int i=1;i<proc;i++){
            count += recvcounts[i-1]; //size of each row: sizeof(int)*size
            recvcounts[i] = getLength(size,proc,i)*sizeof(int)*size;
            displs[i] = count;
        }
        MPI_Gatherv(NULL,0,MPI_BYTE,results,recvcounts,displs,MPI_BYTE,0,MPI_COMM_WORLD);
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) buffer[{i, j}] = results[i*size+j];
        }
        free(recvcounts);
        free(displs);
        free(results);
    }
    else {
        int curr_row = proc; //proc-1 processes -> curr_row = proc-1+1
        int recv_row = 0;
        while(recv_row<size-1){
            /** probe for receive **/
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            recv_row++;
            int src = status.MPI_SOURCE;
            struct Row* row = static_cast<struct Row*>(malloc(sizeof(struct Row)+sizeof(int)*size));
            MPI_Recv(row, sizeof(struct Row)+sizeof(int)*size, MPI_BYTE, src, MPI_ANY_TAG,
            MPI_COMM_WORLD, &status);
            int send_row_index = curr_row++; //the working nodes will stop when receiving a row_index>=size
            MPI_Send(&send_row_index,1,MPI_INT, src, 0, MPI_COMM_WORLD);
            for (int j = 0; j < size; ++j) buffer[{row->index, j}] = row->content[j];
            free(row);
        }
    }
}

void calculate_slave(int rank) {
    auto begin = std::chrono::high_resolution_clock::now();
    struct Parameter* para = static_cast<struct Parameter*>(malloc(sizeof(struct Parameter)));
    MPI_Bcast(para, sizeof(struct Parameter), MPI_BYTE, 0, MPI_COMM_WORLD);
    int size, center_x, center_y, scale, proc, k_value;
    while(para->size){
        size = para->size;
        center_x = para->x_center;
        center_y = para->y_center;
        scale = para->scale;
        proc = para->proc;
        k_value = para->k_value;
        if(!dynamic_flag){
            double cx = static_cast<double>(size) / 2 + center_x;
            double cy = static_cast<double>(size) / 2 + center_y;
            double zoom_factor = static_cast<double>(size) / 4 * scale;
            int length = getLength(size,proc,rank);
            int* my_partition = static_cast<int*>(malloc(length*sizeof(int)*size));
            int start = 0;
            for(int i=1;i<rank;i++) start += getLength(size,proc,i);
            for (int i = 0; i < length; ++i) {
                for (int j = 0; j < size; ++j) {
                    double x = (static_cast<double>(j) - cx) / zoom_factor;
                    double y = (static_cast<double>(i+start) - cy) / zoom_factor;
                    std::complex<double> z{0, 0};
                    std::complex<double> c{x, y};
                    int k = 0;
                    do {
                        z = z * z + c;
                        k++;
                    } while (norm(z) < 2.0 && k < k_value);
                    my_partition[i*size+j] = k;
                }
            }
            MPI_Gatherv(my_partition,length*sizeof(int)*size,MPI_BYTE,NULL,
            NULL,NULL,MPI_BYTE,0,MPI_COMM_WORLD);
            auto end = std::chrono::high_resolution_clock::now();
            std::cout<<"RANK #"<<rank<<" takes " <<duration_cast<std::chrono::nanoseconds>(end - begin).count()<<" ns."<<std::endl;
            free(my_partition);
        }
        else {
            if(rank-1<size){        /** rank-1 because the first rank does not carry out any work **/
                double cx = static_cast<double>(size) / 2 + center_x;
                double cy = static_cast<double>(size) / 2 + center_y;
                double zoom_factor = static_cast<double>(size) / 4 * scale;
                struct Row* row = static_cast<struct Row*>(malloc(sizeof(struct Row)+sizeof(int)*size));
                row->index = rank-1;
                MPI_Status status; int recv_row_index;
                while(row->index<size){             /** exit condition: getting row index >= size **/
                    for (int j = 0; j < size; ++j) {
                        double x = (static_cast<double>(j) - cx) / zoom_factor;
                        double y = (static_cast<double>(row->index) - cy) / zoom_factor;
                        std::complex<double> z{0, 0};
                        std::complex<double> c{x, y};
                        int k = 0;
                        do {
                            z = z * z + c;
                            k++;
                        } while (norm(z) < 2.0 && k < k_value);
                        row->content[j] = k;
                    }
                    MPI_Send(row, sizeof(struct Row)+sizeof(int)*size, MPI_BYTE, 0, 0,MPI_COMM_WORLD);
                    MPI_Recv(&recv_row_index,1,MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    row->index = recv_row_index;
                }
                free(row);
            }
        }
        MPI_Bcast(para, sizeof(struct Parameter), MPI_BYTE, 0, MPI_COMM_WORLD);
    }
}

static constexpr float MARGIN = 4.0f;
static constexpr float BASE_SPACING = 2000.0f;
static constexpr size_t SHOW_THRESHOLD = 500000000ULL;

int main(int argc, char **argv) {
    int rank, proc, res;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    res = MPI_Comm_size(MPI_COMM_WORLD, &proc);
    if (MPI_SUCCESS != res) {
        throw std::runtime_error("failed to get MPI world size");
    }
    int center_x = 0;
    int center_y = 0;
    int size = 800;
    int scale = 1;
    int k_value = 100;
    int c;
    opterr = 0;
    while ((c = getopt (argc, argv, "gds:k:x:y:")) != -1)
        switch (c)
        {
        case 'g':
            gui_flag = 1;
            break;
        case 'd':
            dynamic_flag = 1;
            break;
        case 's':
            size = atoi(optarg);
            break;
        case 'k':
            k_value = atoi(optarg);
            break;
        case 'x':
            center_x = atoi(optarg);
            break;
        case 'y':
            center_y = atoi(optarg);
            break;
        case '?':
            break;
        default:
            break;
    }
    if (0 == rank) {
        Square canvas(100);
        size_t duration = 0;
        size_t pixels = 0;
        struct Parameter* para = static_cast<struct Parameter*>(malloc(sizeof(struct Parameter)));
        if (gui_flag) {
            graphic::GraphicContext context{"CSC4005 A2 MPI Version"};
            context.run([&](graphic::GraphicContext *context [[maybe_unused]], SDL_Window *) {
                {
                    static ImVec4 col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                    auto io = ImGui::GetIO();
                    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("CSC4005 A2 MPI Version", nullptr,
                                ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoResize);
                    ImDrawList *draw_list = ImGui::GetWindowDrawList();
                    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
                                ImGui::GetIO().Framerate);
                    ImGui::DragInt("Center X", &center_x, 1, -4 * size, 4 * size, "%d");
                    ImGui::DragInt("Center Y", &center_y, 1, -4 * size, 4 * size, "%d");
                    ImGui::DragInt("Fineness", &size, 10, 100, 1000, "%d");
                    ImGui::DragInt("Scale", &scale, 1, 1, 100, "%.01f");
                    ImGui::DragInt("K", &k_value, 1, 100, 1000, "%d");
                    ImGui::ColorEdit4("Color", &col.x);
                    {
                        using namespace std::chrono;
                        auto spacing = BASE_SPACING / static_cast<float>(size);
                        auto radius = spacing / 2;
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const ImU32 col32 = ImColor(col);
                        float x = p.x + MARGIN, y = p.y + MARGIN;
                        canvas.resize(size);
                        auto begin = high_resolution_clock::now();
                        calculate(canvas, size, scale, center_x, center_y, k_value, proc, para);
                        auto end = high_resolution_clock::now();
                        pixels += size*size;
                        duration += duration_cast<nanoseconds>(end - begin).count();
                        if (duration > SHOW_THRESHOLD) {
                            std::cout << pixels << " pixels in last " << duration << " nanoseconds\n";
                            auto speed = static_cast<double>(pixels) / static_cast<double>(duration) * 1e9;
                            std::cout << "speed: " << speed << " pixels per second" << std::endl;
                            pixels = 0;
                            duration = 0;
                        }
                        for (int i = 0; i < size; ++i) {
                            for (int j = 0; j < size; ++j) {
                                if (canvas[{i, j}] == k_value) {
                                    draw_list->AddCircleFilled(ImVec2(x, y), radius, col32);
                                }
                                x += spacing;
                            }
                            y += spacing;
                            x = p.x + MARGIN;
                        }
                    }
                    ImGui::End();
                }
            });
        }
        else {
            canvas.resize(size);
            auto begin = std::chrono::high_resolution_clock::now();
            calculate(canvas, size, scale, center_x, center_y, k_value, proc, para);
            auto end = std::chrono::high_resolution_clock::now();
            pixels += size*size;
            duration += duration_cast<std::chrono::nanoseconds>(end - begin).count();
            std::cout << pixels << " pixels in last " << duration << " nanoseconds\n";
            auto speed = static_cast<double>(pixels) / static_cast<double>(duration) * 1e9;
            std::cout << "speed: " << speed << " pixels per second" << std::endl;
            pixels = 0;
            duration = 0;
        }
        para->size = 0;
        MPI_Bcast(para, sizeof(struct Parameter), MPI_BYTE, 0, MPI_COMM_WORLD);
    }
    if(0 != rank) calculate_slave(rank);
    MPI_Finalize();
    return 0;
}
