/**
 * @file log_on_signal.h
 * @brief Reactor that prints a message to stdout on a signal — a debugging/demo affordance.
 */

#ifndef UICOOPA_REACTORS_LOG_ON_SIGNAL_H
#define UICOOPA_REACTORS_LOG_ON_SIGNAL_H

#include <uicoopa/reactors/signal_reactor.h>
#include <iostream>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class LogOnSignal
 * @brief Prints `message` (plus a running fire count) to std::cout when listen_signal fires.
 *
 * @code
 * - type: LogOnSignal
 *   listen_object: ButtonPanel
 *   listen_signal: click
 *   message: "Button clicked"
 * @endcode
 */
class LogOnSignal : public SignalReactor {
public:
    std::string type_name() const override { return "LogOnSignal"; }

    std::string message = "signal fired"; /**< Text to print alongside the fire count. */

protected:
    void on_signal(const coopa::event::EventArgs&) override {
        ++count_;
        std::cout << "[LogOnSignal] " << message << " (" << count_ << " time(s))\n";
    }

private:
    int count_ = 0;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_REACTORS_LOG_ON_SIGNAL_H
