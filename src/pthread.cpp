#include <chrono>
#include <iostream>
#include <graphic/graphic.hpp>
#include <imgui_impl_sdl.h>
#include <vector>
#include <complex>
#include <cstring>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

int dynamic_flag = 0;
int gui_flag = 0;
int thread_num = 10;
int curr_row = 0;
pthread_mutex_t mutex;
//pthread_mutex_t print_mutex;
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

struct parameter_t {
    Square* buffer;
    int size;
    int scale;
    double x_center;
    double y_center;
    int k_value;
    int tid;
};

static inline int getLength(const int &size, const int &thread_num, const int &tid){
    if(size<(thread_num)) return (tid)<size;
    return (size-tid)/thread_num + ((size-tid)%thread_num > 0); // ceil funct
}

void* calculate_slave(void* ptr) {
    //auto begin = std::chrono::high_resolution_clock::now();
    parameter_t* para = static_cast<parameter_t*>(ptr);
    int size = para->size;
    int scale = para->scale;
    int x_center = para->x_center;
    int y_center = para->y_center;
    int k_value = para->k_value;
    int tid = para->tid;
    Square* buffer = para->buffer;
    double cx = static_cast<double>(size) / 2 + x_center;
    double cy = static_cast<double>(size) / 2 + y_center;
    double zoom_factor = static_cast<double>(size) / 4 * scale;
    if(!dynamic_flag) {
        int length = getLength(size,thread_num,tid);
        int start = 0;
        for(int i=0;i<tid;i++) start += getLength(size,thread_num,i);
        for (int i = start; i < start+length; ++i) {
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
                (*buffer)[{i,j}] = k;
            }
        }
    }
    else {
        int my_row;
        pthread_mutex_lock(&mutex);
        while(curr_row<size-1){
            my_row = curr_row++;
            pthread_mutex_unlock(&mutex);
            for (int j = 0; j < size; ++j) {
                double x = (static_cast<double>(j) - cx) / zoom_factor;
                double y = (static_cast<double>(my_row) - cy) / zoom_factor;
                std::complex<double> z{0, 0};
                std::complex<double> c{x, y};
                int k = 0;
                do {
                    z = z * z + c;
                    k++;
                } while (norm(z) < 2.0 && k < k_value);
                (*buffer)[{my_row,j}] = k;
            }
            pthread_mutex_lock(&mutex);
        }
        pthread_mutex_unlock(&mutex);
    }
    //auto end = std::chrono::high_resolution_clock::now();
    //pthread_mutex_lock(&print_mutex);
    //std::cout<<"TID #"<<tid<<" takes " <<duration_cast<std::chrono::nanoseconds>(end - begin).count()<<" ns."<<std::endl;
    //pthread_mutex_unlock(&print_mutex);
    return NULL;
}

void calculate(Square &buffer, int size, int scale, double x_center, double y_center, int k_value) {
    pthread_mutex_init(&mutex,NULL);
    //pthread_mutex_init(&print_mutex,NULL);
    curr_row = 0;
    parameter_t* para = new parameter_t[thread_num];
    pthread_t* threads = new pthread_t[thread_num];
    for(int i=0;i<thread_num;i++){
        para[i].size = size;
        para[i].scale = scale;
        para[i].x_center = x_center;
        para[i].y_center = y_center;
        para[i].k_value = k_value;
        para[i].tid = i;
        para[i].buffer = &buffer;
        pthread_create(threads+i,NULL,calculate_slave,para+i);
    }
    for(int i=0;i<thread_num;i++) pthread_join(threads[i],NULL);
    pthread_mutex_destroy(&mutex);
}
static constexpr float MARGIN = 4.0f;
static constexpr float BASE_SPACING = 2000.0f;
static constexpr size_t SHOW_THRESHOLD = 500000000ULL;
int main(int argc, char **argv) {
    int center_x = 0;
    int center_y = 0;
    int size = 800;
    int scale = 1;
    ImVec4 col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
    int k_value = 100;
    int c;
    opterr = 0;
    while ((c = getopt (argc, argv, "gdt:s:k:x:y:")) != -1)
        switch (c)
        {
        case 'g':
            gui_flag = 1;
            break;
        case 'd':
            dynamic_flag = 1;
            break;
        case 't':
            thread_num = atoi(optarg);
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
    Square canvas(100);
    size_t duration = 0;
    size_t pixels = 0;
    if (gui_flag) {
    graphic::GraphicContext context{"CSC4005 A2 Pthread Version"};
    context.run([&](graphic::GraphicContext *context [[maybe_unused]], SDL_Window *) {
        {
            static ImVec4 col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
            auto io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("CSC4005 A2 Pthread Version", nullptr,
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
                calculate(canvas, size, scale, center_x, center_y, k_value);
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
        calculate(canvas, size, scale, center_x, center_y, k_value);
        auto end = std::chrono::high_resolution_clock::now();
        pixels += size*size;
        duration += duration_cast<std::chrono::nanoseconds>(end - begin).count();
        std::cout << pixels << " pixels in last " << duration << " nanoseconds\n";
        auto speed = static_cast<double>(pixels) / static_cast<double>(duration) * 1e9;
        std::cout << "speed: " << speed << " pixels per second" << std::endl;
        pixels = 0;
        duration = 0;
    }

    return 0;
}

