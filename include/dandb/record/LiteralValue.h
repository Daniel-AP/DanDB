#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace dandb::record {

    class LiteralValue {
        public:
            enum class Kind {
                Null,
                Integer,
                Real,
                String,
                Boolean
            };

            static LiteralValue null();
            static LiteralValue integer(std::int64_t value);
            static LiteralValue real(double value);
            static LiteralValue string(std::string value);
            static LiteralValue boolean(bool value);

            Kind kind() const;
            bool is_null() const;

            std::int64_t as_integer() const;
            double as_real() const;
            const std::string& as_string() const;
            bool as_boolean() const;

        private:
            using Payload = std::variant<
                std::monostate,
                std::int64_t,
                double,
                std::string,
                bool
            >;

            LiteralValue(Kind kind, Payload payload);

            Kind kind_;
            Payload payload_;
    };

}
