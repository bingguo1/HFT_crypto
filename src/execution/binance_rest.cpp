#include "hfmm/execution/binance_rest.hpp"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace hfmm {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// CURL write callback
// ---------------------------------------------------------------------------
std::size_t BinanceRest::write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
    auto* buf = static_cast<std::string*>(user);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
BinanceRest::BinanceRest(const Config& cfg) : cfg_(cfg) {
    if (!cfg_.paper_trading) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ = curl_easy_init();
        if (!curl_) throw std::runtime_error("Failed to init libcurl");
    }
}

BinanceRest::~BinanceRest() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_global_cleanup();
    }
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------
RestResponse BinanceRest::get(const std::string& path, const std::string& query) {
    RestResponse resp;
    if (!curl_) return resp;

    std::string url = cfg_.rest_endpoint + path;
    if (!query.empty()) url += "?" + query;

    response_buf_.clear();
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_buf_);

    struct curl_slist* headers = nullptr;
    if (!cfg_.api_key.empty()) {
        headers = curl_slist_append(headers, ("X-MBX-APIKEY: " + cfg_.api_key).c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode rc = curl_easy_perform(curl_);
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &resp.http_code);
    if (headers) curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
        resp.http_code = 0;
        resp.body = curl_easy_strerror(rc);
    } else {
        resp.body = response_buf_;
    }
    return resp;
}

RestResponse BinanceRest::post(const std::string& path, const std::string& body) {
    RestResponse resp;
    if (!curl_) return resp;

    std::string url = cfg_.rest_endpoint + path;

    response_buf_.clear();
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_buf_);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (!cfg_.api_key.empty()) {
        headers = curl_slist_append(headers, ("X-MBX-APIKEY: " + cfg_.api_key).c_str());
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl_);
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &resp.http_code);
    curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
        resp.http_code = 0;
        resp.body = curl_easy_strerror(rc);
    } else {
        resp.body = response_buf_;
    }
    return resp;
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 signing
// ---------------------------------------------------------------------------
std::string BinanceRest::sign(const std::string& data) const {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int  digest_len = 0;

    HMAC(EVP_sha256(),
         cfg_.api_secret.c_str(), static_cast<int>(cfg_.api_secret.size()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
         digest, &digest_len);

    std::ostringstream oss;
    for (unsigned i = 0; i < digest_len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    return oss.str();
}

std::string BinanceRest::signed_query(const std::string& params) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
    std::string full = params + "&timestamp=" + std::to_string(now_ms);
    return full + "&signature=" + sign(full);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
RestResponse BinanceRest::fetch_depth_snapshot(const std::string& symbol, int limit) {
    if (cfg_.paper_trading && !curl_) {
        // In paper mode without curl, we still need to fetch snapshot
        // Fall through to normal path if curl available, else return empty
        return {};
    }
    return get("/api/v3/depth",
               "symbol=" + symbol + "&limit=" + std::to_string(limit));
}

int64_t BinanceRest::fetch_server_time() {
    if (!curl_) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    auto resp = get("/api/v3/time");
    if (!resp.ok()) return -1;
    try {
        auto j = json::parse(resp.body);
        return j.at("serverTime").get<int64_t>();
    } catch (...) {
        return -1;
    }
}

OrderResponse BinanceRest::place_order(const std::string& symbol, Side side, Price price, Quantity qty) {
    if (cfg_.paper_trading) {
        // Synthetic response
        OrderResponse r;
        r.order_id  = next_paper_id_++;
        r.success   = true;
        return r;
    }

    std::string side_str   = (side == Side::Buy) ? "BUY" : "SELL";
    double price_d = from_price(price);
    double qty_d   = from_qty(qty);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    oss << "symbol=" << symbol
        << "&side="     << side_str
        << "&type=LIMIT"
        << "&timeInForce=GTC"
        << "&price="    << price_d
        << "&quantity=" << qty_d;

    std::string body = signed_query(oss.str());
    auto resp = post("/api/v3/order", body);

    OrderResponse r;
    if (!resp.ok()) return r;
    try {
        auto j = json::parse(resp.body);
        r.order_id         = j.at("orderId").get<uint64_t>();
        r.client_order_id  = j.at("clientOrderId").get<std::string>();
        r.success          = true;
    } catch (...) {}
    return r;
}

bool BinanceRest::cancel_order(const std::string& symbol, uint64_t order_id) {
    if (cfg_.paper_trading) return true;

    std::string params = "symbol=" + symbol +
                         "&orderId=" + std::to_string(order_id);
    std::string body = signed_query(params);

    // DELETE request — use CURL custom request
    if (!curl_) return false;
    std::string url = cfg_.rest_endpoint + "/api/v3/order?" + body;
    response_buf_.clear();
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_buf_);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-MBX-APIKEY: " + cfg_.api_key).c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl_);
    long code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);

    return rc == CURLE_OK && code == 200;
}

} // namespace hfmm
