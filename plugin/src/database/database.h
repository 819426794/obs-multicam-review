#pragma once

#include <string>
#include <mutex>
#include <vector>

struct sqlite3;

namespace multicam {

class Database {
public:
    Database() = default;
    ~Database();

    // 禁止拷贝
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // 生命周期
    bool open(const char *db_path);
    void close();
    bool init_schema();

    // ============ Project ============
    bool project_create(const char *json_in, char **json_out);
    bool project_get(const char *id, char **json_out);
    bool project_list(char **json_out);
    bool project_update(const char *id, const char *json_in);
    bool project_delete(const char *id);

    // ============ Product ============
    bool product_create(const char *project_id, const char *json_in, char **json_out);
    bool product_get(const char *id, char **json_out);
    bool product_list(const char *project_id, char **json_out);
    bool product_update(const char *id, const char *json_in);
    bool product_delete(const char *id);
    bool product_reorder(const char *project_id, const char *json_ids);

    // ============ Dimension Template ============
    bool dim_template_create(const char *json_in, char **json_out);
    bool dim_template_get(const char *id, char **json_out);
    bool dim_template_list(char **json_out);
    bool dim_template_delete(const char *id);
    bool dim_item_create(const char *template_id, const char *json_in);
    bool dim_item_list(const char *template_id, char **json_out);

    // ============ Product-Dimension Binding ============
    bool binding_set(const char *product_id, const char *template_id);
    bool binding_get(const char *product_id, char **json_out);

    // ============ Scoring ============
    bool scoring_session_create(const char *json_in, char **json_out);
    bool scoring_session_get(const char *id, char **json_out);
    bool scoring_session_complete(const char *id);
    bool score_submit(const char *session_id, const char *product_id,
                      const char *dim_key, double score, const char *note);
    bool score_get_by_product(const char *session_id, const char *product_id, char **json_out);
    bool score_get_leaderboard(const char *session_id, char **json_out);

private:
    sqlite3 *db_ = nullptr;
    std::mutex mutex_;
};

} // namespace multicam
