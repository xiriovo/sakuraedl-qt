#pragma once

#include <QString>

namespace sakura {

class FirehoseClient;

// ─── Abstract authentication strategy for vendor-locked loaders ──────
class IAuthStrategy {
public:
    virtual ~IAuthStrategy() = default;

    // Perform authentication against the firehose client.
    // Returns true if the device accepted the auth challenge.
    virtual bool authenticateAsync(FirehoseClient* client) = 0;

    // Human-readable name for this auth strategy
    virtual QString name() const = 0;
};

} // namespace sakura
