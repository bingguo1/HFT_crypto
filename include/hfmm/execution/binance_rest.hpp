#pragma once
#include <string>
#include <curl/curl.h>
#include "hfmm/core/types.hpp"

namespace hfmm {

struct RestResponse {
    long        http_code{0};
    std::string body;
    bool        ok() const { return http_code == 200; }
};

struct OrderResponse {
    uint64_t    order_id{0};
    std::string client_order_id;
    bool        success{false};
};

class BinanceRest {
public:
    explicit BinanceRest(const Config& cfg);
    ~BinanceRest();

    // Non-copyable
    BinanceRest(const BinanceRest&) = delete;
    BinanceRest& operator=(const BinanceRest&) = delete;

    // Fetch depth snapshot (no auth required)
    RestResponse fetch_depth_snapshot(const std::string& symbol, int limit = 1000);

    // Fetch server time (ms)
    int64_t fetch_server_time();

    // Place limit order — paper mode returns synthetic response
    OrderResponse place_order(const std::string& symbol, Side side, Price price, Quantity qty);

    // Cancel order by ID
    bool cancel_order(const std::string& symbol, uint64_t order_id);

private:
    RestResponse get(const std::string& path, const std::string& query = "");
    RestResponse post(const std::string& path, const std::string& body);

    // Sign query string with HMAC-SHA256
    std::string sign(const std::string& data) const;

    // Add timestamp + signature to query string
    std::string signed_query(const std::string& params);

    static std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* user);

    const Config& cfg_;
    CURL*         curl_{nullptr};
    std::string   response_buf_;
    uint64_t      next_paper_id_{1};
};

} // namespace hfmm
