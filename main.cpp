#include <curl/curl.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

std::string game_id;

struct Mod {
  std::string name;
  std::string id;
  std::string game_id;

  bool operator==(const Mod& other) const {
    return name == other.name && id == other.id && game_id == other.game_id;
  }
};

std::vector<Mod> mods;

namespace nlohmann {
void to_json(json& j, const Mod& mod) {
  j = json{{"name", mod.name}, {"id", mod.id}, {"game_id", mod.game_id}};
}

void from_json(const json& j, Mod& mod) {
  j.at("name").get_to(mod.name);
  j.at("id").get_to(mod.id);
  j.at("game_id").get_to(mod.game_id);
}
}  // namespace nlohmann

void fill_list_box(GtkListBox* list_box, const std::vector<Mod>& mods) {
  for (const Mod& mod : mods) {
    std::string lname = mod.name;
    GtkWidget* label = gtk_label_new(lname.c_str());
    gtk_container_add(GTK_CONTAINER(list_box), label);
  }
}

void on_entry_activate(GtkEntry* entry, GtkDialog* dialog) {
  gtk_dialog_response(dialog, GTK_RESPONSE_ACCEPT);
}

void load_mods() {
  std::ifstream i("modlist.json");
  if (i.is_open()) {
    nlohmann::json j;
    i >> j;
    mods = j.get<std::vector<Mod>>();
  }
}

void save_mods() {
  nlohmann::json j = mods;
  std::ofstream o("modlist.json");
  o << j << std::endl;
}

static void download_item(const std::string& item_id) {
  const char* user = std::getenv("USER");

  if (!user) {
    std::cerr << "Unable to get user" << std::endl;
    return;
  }

  std::string path = "/home/" + std::string(user) +
                     "/Steam/steamapps/workshop/content/" + game_id + "/" +
                     item_id;

  if (std::filesystem::exists(path)) {
    std::cerr << "Mod already downloaded" << std::endl;
    return;
  }

  std::string command =
      "./steamcmd/steamcmd.sh +login anonymous +workshop_download_item " +
      game_id + " " + item_id + " +quit";

  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    std::cerr << "popen failed" << std::endl;
    return;
  }

  char buffer[128];
  std::string result = "";
  while (!feof(pipe)) {
    if (fgets(buffer, 128, pipe) != NULL)
      result += buffer;
  }

  pclose(pipe);

  // if buffer contains ERROR! then cerr
  if (result.find("ERROR!") != std::string::npos) {
    std::cerr << "Mod id: " << item_id << " failed." << std::endl;
    std::cerr << result << std::endl;

    GtkWidget* dialog = gtk_message_dialog_new(
        NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "Failed to download item: %s", item_id.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  }
}

static gboolean show_finished_dialog(gpointer data) {
  GtkWidget* dialog = gtk_message_dialog_new(
      NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      "Finished downloading");
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  return FALSE;
}

static gpointer download_all_thread(gpointer data) {
  GtkButton* button = GTK_BUTTON(data);

  if (mods.empty()) {
    return NULL;
  }

  for (auto& mod : mods) {
    download_item(mod.id);
  }

  g_idle_add(show_finished_dialog, NULL);

  return NULL;
}

static void download_all(GtkButton* button, gpointer user_data) {
  g_thread_new(NULL, download_all_thread, button);
}

// fixes forward and back buttons in browser
static gboolean on_button_press_event(GtkWidget* widget, GdkEventButton* event,
                                      gpointer user_data) {
  WebKitWebView* web_view = WEBKIT_WEB_VIEW(widget);

  if (event->button == 8) {
    if (webkit_web_view_can_go_back(web_view)) {
      webkit_web_view_go_back(web_view);
    }
    return TRUE;
  } else if (event->button == 9) {
    if (webkit_web_view_can_go_forward(web_view)) {
      webkit_web_view_go_forward(web_view);
    }
    return TRUE;
  }

  return FALSE;
}

static void remove_item_from_list(GtkButton* button, gpointer user_data) {
  GtkListBox* list_box = GTK_LIST_BOX(user_data);
  GtkListBoxRow* row = gtk_list_box_get_selected_row(list_box);

  if (row != NULL) {
    int index = gtk_list_box_row_get_index(row);

    mods.erase(mods.begin() + index);

    gtk_widget_destroy(GTK_WIDGET(row));
  }
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                            std::string* out) {
  size_t realsize = size * nmemb;
  out->append((char*)contents, realsize);
  return realsize;
}

std::string get_workshop_game_name() {
  CURL* curl = curl_easy_init();
  std::string read_buffer;

  if (curl) {
    std::string url = "https://api.steampowered.com/ISteamApps/GetAppList/v2/";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }

  auto json = nlohmann::json::parse(read_buffer);
  auto apps = json["applist"]["apps"];

  for (auto& app : apps) {
    int appid = app["appid"].get<int>();

    if (std::to_string(appid) == game_id) {
      return app["name"].get<std::string>();
    }
  }

  return std::string();
}

static std::string get_workshop_item_name(const std::string& item_id) {
  CURL* curl = curl_easy_init();
  std::string read_buffer;

  if (curl) {
    std::string url =
        "https://api.steampowered.com/ISteamRemoteStorage/"
        "GetPublishedFileDetails/v1/";
    std::string postData = "itemcount=1&publishedfileids[0]=" + item_id;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }

  auto json = nlohmann::json::parse(read_buffer);
  auto details = json["response"]["publishedfiledetails"];

  if (!details.empty()) {
    return details[0]["title"].get<std::string>();
  }

  else {
    return std::string();
  }
}

static void add_item_to_list(GtkButton* button, gpointer user_data) {
  GtkWidget* button_box = gtk_widget_get_parent(GTK_WIDGET(button));
  GtkWidget* box = gtk_widget_get_parent(GTK_WIDGET(button_box));
  GtkWidget* grid = gtk_widget_get_parent(GTK_WIDGET(box));
  GtkWidget* browser = gtk_grid_get_child_at(GTK_GRID(grid), 0, 1);

  std::string address = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(browser));
  size_t id_location = address.find("id=");

  std::string result = "";

  for (size_t i = id_location + 3; i < address.length(); i++) {
    if (isdigit(address[i])) {
      result += address[i];
    } else {
      break;
    }
  }

  std::string name = get_workshop_item_name(result);

  Mod mod;
  mod.name = name;
  mod.id = result;
  mod.game_id = game_id;

  if (std::find(mods.begin(), mods.end(), mod) == mods.end()) {
    mods.push_back(mod);

    std::string lname = name;

    GtkListBox* list_box = GTK_LIST_BOX(user_data);
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new(lname.c_str());

    gtk_container_add(GTK_CONTAINER(row), label);
    gtk_list_box_insert(list_box, row, -1);
    gtk_widget_show_all(row);
  }

  else {
    std::cerr << "Mod already added" << std::endl;
  }
}

static void show_browser(GtkWidget* grid) {
  WebKitWebView* browser = WEBKIT_WEB_VIEW(webkit_web_view_new());

  std::string url = "https://steamcommunity.com/app/" + game_id + "/workshop/";

  webkit_web_view_load_uri(browser, url.c_str());
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(browser), 0, 1, 3, 1);
  gtk_widget_show(GTK_WIDGET(browser));

  g_signal_connect(browser, "button-press-event",
                   G_CALLBACK(on_button_press_event), NULL);

  GtkWidget* scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget* list_box = gtk_list_box_new();
  fill_list_box(GTK_LIST_BOX(list_box), mods);
  gtk_container_add(GTK_CONTAINER(scrolled_window), list_box);

  // TODO: replace + and - with icons
  GtkWidget* add_button = gtk_button_new_with_label("+");
  gtk_widget_set_halign(add_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(add_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(add_button, 5);
  gtk_widget_set_margin_bottom(add_button, 5);
  gtk_widget_set_margin_start(add_button, 5);
  gtk_widget_set_margin_end(add_button, 5);

  g_signal_connect(add_button, "clicked", G_CALLBACK(add_item_to_list),
                   list_box);

  GtkWidget* subtract_button = gtk_button_new_with_label("-");
  gtk_widget_set_halign(subtract_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(subtract_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(subtract_button, 5);
  gtk_widget_set_margin_bottom(subtract_button, 5);
  gtk_widget_set_margin_start(subtract_button, 5);
  gtk_widget_set_margin_end(subtract_button, 5);

  g_signal_connect(subtract_button, "clicked",
                   G_CALLBACK(remove_item_from_list), list_box);

  GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_pack_start(GTK_BOX(button_box), add_button, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(button_box), subtract_button, TRUE, TRUE, 0);

  GtkWidget* button_download = gtk_button_new_with_label("Download All");
  gtk_widget_set_margin_top(button_download, 5);
  gtk_widget_set_margin_bottom(button_download, 5);
  gtk_widget_set_margin_start(button_download, 5);
  gtk_widget_set_margin_end(button_download, 5);

  g_signal_connect(button_download, "clicked", G_CALLBACK(download_all), NULL);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box), button_box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), button_download, FALSE, FALSE, 0);

  gtk_grid_attach(GTK_GRID(grid), box, 3, 1, 1, 1);
  gtk_widget_show_all(box);
}

static void download_steamcmd(GtkButton* button, gpointer user_data) {
  gtk_button_set_label(button, "Downloading...");
  gtk_widget_queue_draw(GTK_WIDGET(button));

  while (gtk_events_pending()) {
    gtk_main_iteration();
  }

  std::filesystem::remove_all("steamcmd");
  std::filesystem::create_directory("steamcmd");

  system(
      "curl -o steamcmd/steamcmd_linux.tar.gz "
      "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz");
  system("tar zxvf steamcmd/steamcmd_linux.tar.gz -C steamcmd");
  system("chmod +x ./steamcmd/steamcmd.sh");
  system("./steamcmd/steamcmd.sh +quit");

  GtkWidget* parent = gtk_widget_get_parent(GTK_WIDGET(button));
  if (parent) {
    gtk_container_remove(GTK_CONTAINER(parent), GTK_WIDGET(button));
    show_browser(GTK_WIDGET(parent));
  }
}

// Initialize GTK and start application
static void activate(GtkApplication* app, gpointer user_data) {
  GtkWidget* window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Steam Harvester");
  gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);

  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

  GtkWidget* download_button = gtk_button_new_with_label("Download SteamCMD");
  gtk_widget_set_halign(download_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(download_button, GTK_ALIGN_CENTER);

  gtk_grid_attach(GTK_GRID(grid), download_button, 0, 0, 1, 1);

  if (!std::filesystem::exists("steamcmd/steamcmd.sh")) {
    g_signal_connect(download_button, "clicked", G_CALLBACK(download_steamcmd),
                     NULL);
  }

  else {
    gtk_widget_destroy(GTK_WIDGET(download_button));
    show_browser(grid);
  }

  bool valid = false;

  while (!valid) {
    GtkWidget* dialog =
        gtk_dialog_new_with_buttons("Steam Harvester", NULL, GTK_DIALOG_MODAL,
                                    "OK", GTK_RESPONSE_ACCEPT, NULL);
    gtk_widget_set_size_request(dialog, 200, 100);
    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget* game_id_entry = gtk_entry_new();

    g_signal_connect(game_id_entry, "activate", G_CALLBACK(on_entry_activate),
                     dialog);

    std::string label_text = "Enter Game ID to manage:";

    if (!game_id.empty()) {
      gtk_entry_set_text(GTK_ENTRY(game_id_entry), game_id.c_str());
    }

    else {
      gtk_entry_set_text(GTK_ENTRY(game_id_entry), "Enter Game ID to manage:");
    }

    gtk_container_add(GTK_CONTAINER(content_area), game_id_entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      game_id = gtk_entry_get_text(GTK_ENTRY(game_id_entry));
      valid = std::all_of(game_id.begin(), game_id.end(), ::isdigit);
      if (valid) {
        std::ofstream file("config.txt");
        if (file) {
          file << game_id;
        }

        file.close();
      }
    } else {
      break;
    }

    gtk_widget_destroy(dialog);

    std::string game_name = "Currently Managing: " + get_workshop_game_name();
    if (!game_name.empty()) {
      gtk_window_set_title(GTK_WINDOW(window), game_name.c_str());
    }
  }

  gtk_container_add(GTK_CONTAINER(window), grid);

  gtk_widget_show_all(window);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char** argv) {
  GtkApplication* app;
  int status;

  load_mods();

  std::ifstream file("config.txt");
  if (file) {
    std::getline(file, game_id);
  }

  file.close();

  app = gtk_application_new("xyz.tpby", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  save_mods();

  return status;
}