// SPDX-License-Identifier: GPL-3.0-or-later

#include "toolchange_wear_state.h"

#include "helix/xml/scoped_subject_registry.h"
#include "state/subject_macros.h"

#include <spdlog/fmt/fmt.h>

#include <cstring>

namespace helix {

namespace {

// Subject names are held by pointer, so they must outlive registration.
constexpr std::array<std::array<const char*, 3>, toolchange_wear::kHeads> kHeadSubjectNames = {{
    {"toolchange_wear_h0_pct", "toolchange_wear_h0_status", "toolchange_wear_h0_text"},
    {"toolchange_wear_h1_pct", "toolchange_wear_h1_status", "toolchange_wear_h1_text"},
    {"toolchange_wear_h2_pct", "toolchange_wear_h2_status", "toolchange_wear_h2_text"},
    {"toolchange_wear_h3_pct", "toolchange_wear_h3_status", "toolchange_wear_h3_text"},
}};

void register_named(SubjectManager& subjects, lv_subject_t* subject, const char* name,
                    bool register_xml) {
    subjects.register_subject(subject, register_xml ? name : nullptr);
    if (register_xml) {
        helix::xml::register_subject_in_current_scope(name, subject);
    }
}

template <typename T> void keep_news(std::optional<T>& last, const std::optional<T>& news) {
    if (news) {
        last = news;
    }
}

} // namespace

void ToolchangeWearState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        return;
    }
    INIT_SUBJECT_INT(toolchange_wear_available, 0, subjects_, register_xml);
    for (size_t i = 0; i < heads_.size(); ++i) {
        HeadSubjects& h = heads_[i];
        lv_subject_init_int(&h.pct, 0);
        register_named(subjects_, &h.pct, kHeadSubjectNames[i][0], register_xml);
        lv_subject_init_int(&h.status, static_cast<int>(toolchange_wear::Status::Normal));
        register_named(subjects_, &h.status, kHeadSubjectNames[i][1], register_xml);
        lv_subject_init_string(&h.text, h.text_buf, nullptr, sizeof(h.text_buf), "--");
        register_named(subjects_, &h.text, kHeadSubjectNames[i][2], register_xml);
    }
    subjects_initialized_ = true;
}

void ToolchangeWearState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void ToolchangeWearState::set_hardware(const PrinterDiscovery& hw) {
    supported_ = toolchange_wear::supported(hw);
    last_ = toolchange_wear::Reading{};
    publish();
}

void ToolchangeWearState::apply(const toolchange_wear::Reading& r) {
    if (!supported_) {
        return;
    }
    bool news = false;
    for (size_t i = 0; i < last_.size(); ++i) {
        news = news || r[i].switches || r[i].serviced_at;
        keep_news(last_[i].switches, r[i].switches);
        keep_news(last_[i].serviced_at, r[i].serviced_at);
    }
    if (news) {
        publish();
    }
}

std::optional<int> ToolchangeWearState::since_service(int head) const {
    if (head < 0 || head >= toolchange_wear::kHeads) {
        return std::nullopt;
    }
    const auto& h = last_[static_cast<size_t>(head)];
    if (!h.switches || !h.serviced_at) {
        return std::nullopt;
    }
    return toolchange_wear::since_service(*h.switches, *h.serviced_at);
}

void ToolchangeWearState::publish() {
    if (!subjects_initialized_) {
        return;
    }
    bool any = false;
    for (int i = 0; i < toolchange_wear::kHeads; ++i) {
        HeadSubjects& h = heads_[static_cast<size_t>(i)];
        const auto since = since_service(i);
        any = any || since.has_value();
        const std::string text =
            since ? fmt::format("{} / {}", *since, toolchange_wear::kServiceInterval) : "--";
        if (std::strcmp(lv_subject_get_string(&h.text), text.c_str()) != 0) {
            lv_subject_copy_string(&h.text, text.c_str());
        }
        lv_subject_set_int(&h.pct, since ? toolchange_wear::used_pct(*since) : 0);
        lv_subject_set_int(&h.status, static_cast<int>(since ? toolchange_wear::status_of(*since)
                                                             : toolchange_wear::Status::Normal));
    }
    lv_subject_set_int(&toolchange_wear_available_, supported_ && any ? 1 : 0);
}

} // namespace helix
