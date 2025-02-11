#include <iostream> 
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <thread>  // For multithreading
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

std::mutex mtx;
std::recursive_mutex mutx;
std::atomic<bool> data_downloaded(false);
std::vector<std::string> favorite_titles;
std::unordered_map<std::string, std::string> news_data;

static std::string selected_title;  // Global variable for selected news title
const std::string favorites_file = "favorites.txt";  // Global constant for file name

// Function declarations
void render_gui();
void download_news(const std::string& url);
void fetch_news_async(const std::string& url);
void save_favorites_to_file(const std::string& filename);
void remove_favorite_from_file(const std::string& filename, const std::string& title_to_remove);
void clear_favorites_file(const std::string& filename);

// Helper function to convert a string to lowercase

static std::string to_lowercase(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), [](unsigned char c) {
        return std::tolower(c);
        });
    return lower_str;
}

// Download news asynchronously using a separate thread
void download_news(const std::string& url) {
    std::cout << "Fetching news in background thread...\n";
    httplib::SSLClient client("newsapi.org");
    client.enable_server_certificate_verification(false);

    auto res = client.Get(url.c_str());

    if (res && res->status == 200) {
        try {
            auto news_json = json::parse(res->body);
            {
                std::lock_guard<std::mutex> guard(mtx);
                if (news_json.contains("articles") && news_json["articles"].is_array()) {
                    news_data.clear();
                    for (const auto& item : news_json["articles"]) {
                        std::string title = item.contains("title") && !item["title"].is_null()
                            ? item["title"].get<std::string>()
                            : "No Title";
                        std::string details = item.contains("description") && !item["description"].is_null()
                            ? item["description"].get<std::string>()
                            : "No Description";
                        news_data[title] = details;
                    }
                    data_downloaded = true;
                }
            }
            std::cout << "News successfully downloaded in thread.\n";
        }
        catch (const json::parse_error& e) {
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        }
    }
}

// Launch news download in a separate thread
void fetch_news_async(const std::string& url) {
    std::thread news_thread(download_news, url);
    news_thread.detach();  // Allows the thread to run independently
}

// Save favorite titles to file
void save_favorites_to_file(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(mutx);
    std::ofstream file(filename);
    if (file.is_open()) {
        for (const auto& title : favorite_titles) {
            file << title << std::endl;
        }
        file.close();
    }
}

// Remove a favorite title from file
void remove_favorite_from_file(const std::string& filename, const std::string& title_to_remove) {
    std::lock_guard<std::recursive_mutex> lock(mutx);
    std::ifstream file_in(filename);
    if (!file_in.is_open()) return;

    std::vector<std::string> updated_titles;
    std::string title;
    while (std::getline(file_in, title)) {
        if (title != title_to_remove) {
            updated_titles.push_back(title);
        }
    }

    file_in.close();
    std::ofstream file_out(filename);
    if (file_out.is_open()) {
        for (const auto& updated_title : updated_titles) {
            file_out << updated_title << std::endl;
        }
    }
}

// Render GUI using ImGui
void render_gui() {
    static char search_buffer[256] = "";
    static bool show_search_results = false;

    ImGui::Begin("News Viewer");

    if (data_downloaded) {
        ImGui::Text("News Titles:");
        std::lock_guard<std::mutex> lock(mtx);
        int index = 0;
        for (const auto& [title, details] : news_data) {
            std::string unique_id = "##news_" + std::to_string(index++);
            if (ImGui::Selectable((title + unique_id).c_str(), selected_title == title)) {
                selected_title = title;
            }
        }
    }
    else {
        ImGui::Text("No news available.");
    }

    ImGui::Separator();

    if (!selected_title.empty()) {
        ImGui::Text("Details:");
        if (news_data.find(selected_title) != news_data.end()) {
            ImGui::TextWrapped("%s", news_data[selected_title].c_str());
            if (ImGui::Button("Add to Favorites##AddButton")) {
                if (std::find(favorite_titles.begin(), favorite_titles.end(), selected_title) == favorite_titles.end()) {
                    favorite_titles.push_back(selected_title);
                    save_favorites_to_file(favorites_file);
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Search:");
    ImGui::SameLine();
    if (ImGui::InputText("##SearchBox", search_buffer, IM_ARRAYSIZE(search_buffer))) {
        show_search_results = true;
    }
    std::string search_query = to_lowercase(search_buffer);

    if (show_search_results && !search_query.empty()) {
        ImGui::Text("Search Results:");
        std::lock_guard<std::mutex> lock(mtx);
        int search_index = 0;
        for (const auto& [title, details] : news_data) {
            if (to_lowercase(title).find(search_query) != std::string::npos) {
                std::string unique_id = "##search_" + std::to_string(search_index++);
                if (ImGui::Selectable((title + unique_id).c_str(), selected_title == title)) {
                    selected_title = title;
                    show_search_results = false;
                    search_buffer[0] = '\0';
                    break;
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Favorites:");
    std::lock_guard<std::mutex> lock(mtx);
    int favorite_index = 0;

    for (auto it = favorite_titles.begin(); it != favorite_titles.end();) {
        std::string unique_id = "##favorite_" + std::to_string(favorite_index++);
        bool is_selected = (selected_title == *it);

        if (ImGui::Selectable(((*it) + unique_id).c_str(), is_selected)) {
            selected_title = *it;
        }

        ImGui::SameLine();
        if (ImGui::Button(("Remove##" + unique_id).c_str())) {
            std::string title_to_remove = *it;
            it = favorite_titles.erase(it);
            remove_favorite_from_file(favorites_file, title_to_remove);
        }
        else {
            ++it;
        }
    }

    ImGui::End();
}

// Clear the favorites file at startup
void clear_favorites_file(const std::string& filename) {
    std::ofstream file(filename, std::ios::trunc);
    if (file.is_open()) {
        file.close();
        std::cout << "Favorites file cleared.\n";
    }
}

// Main function
int main() {
    clear_favorites_file(favorites_file);

    const std::string url = "https://newsapi.org/v2/top-headlines?country=us&apiKey=2bb1b1fb207d4504b3a8bc8ac2bd33b6";
    std::cout << "Starting async news fetch...\n";
    fetch_news_async(url);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "News Viewer", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_gui();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
