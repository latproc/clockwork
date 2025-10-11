#include <string>
#include <sstream>
#include <iomanip>
#include "urlencode.h"

std::string url_encode(const std::string &str) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return encoded.str();
}

std::string url_decode(const std::string &str) {
    std::ostringstream decoded;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.size()) {
                int c;
                std::istringstream hex(str.substr(i + 1, 2));
                hex >> std::hex >> c;
                decoded << static_cast<char>(c);
                i += 2;
            }
        } else {
            decoded << str[i];
        }
    }
    return decoded.str();
}

tl::expected<std::string, std::string> url_encode_json(cJSON *json){
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    if (json->type == cJSON_Object) {
        cJSON *child = json->child;
        while (child) {
            switch (child->type) {
                case cJSON_String:
                    encoded << url_encode(child->string) << "=" << url_encode(child->valuestring);
                    break;
                case cJSON_Number:
                    if (child->valueNumber.kind == cJSON_Number_int_t) {
                        encoded << url_encode(child->string) << "=" << url_encode(std::to_string(child->valueint));
                    } else {
                        encoded << url_encode(child->string) << "=" << url_encode(std::to_string(child->valuedouble));
                    }
                    break;
                case cJSON_True:
                    encoded << url_encode(child->string) << "=" << url_encode("true");
                    break;
                case cJSON_False:
                    encoded << url_encode(child->string) << "=" << url_encode("false");
                    break;
                case cJSON_NULL:
                    encoded << url_encode(child->string) << "=" << url_encode("null");
                    break;
                default:
                    return tl::unexpected<std::string>("cannot url encode nested JSON objects or arrays");
                    break;
            }
            child = child->next;
            if (child) {
                encoded << "&";
            }
        }
    }
    else {
        std::ostringstream error;
        auto json_str = cJSON_PrintUnformatted(json);
        error << "url_encode_json expects a JSON object, got: " << json_str;
        free(json_str);
        return tl::unexpected<std::string>(error.str());
    }

    return encoded.str();
}

