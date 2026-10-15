//! C ABI boundary for Island's native ad and tracker blocker.
//!
//! The public surface intentionally stays tiny. Filter syntax, request parsing,
//! third-party classification, and matching all belong to Brave's `adblock`
//! crate; C++ only owns lifecycle, policy, and CEF integration.

use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions, RuleTypes};
use adblock::request::Request;
use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};

const CATEGORY_ADS: u8 = 1;
const CATEGORY_TRACKERS: u8 = 2;

const RESULT_BLOCKED: u32 = 1 << 0;
const RESULT_EXCEPTION: u32 = 1 << 1;
const RESULT_IMPORTANT: u32 = 1 << 2;
const RESULT_ADS: u32 = 1 << 3;
const RESULT_TRACKERS: u32 = 1 << 4;
const RESULT_ERROR: u32 = 1 << 31;

pub struct IslandAdblockEngine {
    ads: Engine,
    trackers: Engine,
    combined: Engine,
    ads_rules: u32,
    tracker_rules: u32,
}

fn parse_options() -> ParseOptions {
    ParseOptions {
        rule_types: RuleTypes::NetworkOnly,
        ..ParseOptions::default()
    }
}

fn build_engine(lists: impl IntoIterator<Item = String>) -> Engine {
    let mut set = FilterSet::new(false);
    for list in lists {
        set.add_filter_list(list, parse_options());
    }
    Engine::new_with_filter_set(set)
}

fn count_rules(text: &str) -> u32 {
    text.lines()
        .filter(|line| adblock::lists::parse_filter(line, false, parse_options()).is_ok())
        .count()
        .try_into()
        .unwrap_or(u32::MAX)
}

unsafe fn required_text<'a>(value: *const c_char) -> Option<&'a str> {
    if value.is_null() {
        return None;
    }
    CStr::from_ptr(value).to_str().ok()
}

/// Compile EasyList and EasyPrivacy text into category-specific and combined engines.
#[no_mangle]
pub unsafe extern "C" fn island_adblock_engine_create(
    ads: *const c_char,
    trackers: *const c_char,
) -> *mut IslandAdblockEngine {
    catch_unwind(AssertUnwindSafe(|| {
        let ads = required_text(ads)?.to_owned();
        let trackers = required_text(trackers)?.to_owned();
        let handle = IslandAdblockEngine {
            ads: build_engine([ads.clone()]),
            trackers: build_engine([trackers.clone()]),
            combined: build_engine([ads.clone(), trackers.clone()]),
            ads_rules: count_rules(&ads),
            tracker_rules: count_rules(&trackers),
        };
        Some(Box::into_raw(Box::new(handle)))
    }))
    .ok()
    .flatten()
    .unwrap_or(std::ptr::null_mut())
}

/// Match a request. Strings are UTF-8 NUL-terminated values owned by the caller.
#[no_mangle]
pub unsafe extern "C" fn island_adblock_engine_check(
    engine: *const IslandAdblockEngine,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
    category_mask: u8,
) -> u32 {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = engine.as_ref() else {
            return RESULT_ERROR;
        };
        let (Some(url), Some(source_url), Some(request_type)) = (
            required_text(url),
            required_text(source_url),
            required_text(request_type),
        ) else {
            return RESULT_ERROR;
        };
        let Ok(request) = Request::new(url, source_url, request_type, "GET") else {
            return RESULT_ERROR;
        };
        const CATEGORY_BOTH: u8 = CATEGORY_ADS | CATEGORY_TRACKERS;
        let selected = match category_mask & CATEGORY_BOTH {
            CATEGORY_ADS => &engine.ads,
            CATEGORY_TRACKERS => &engine.trackers,
            CATEGORY_BOTH => &engine.combined,
            _ => return 0,
        };
        let result = selected.check_network_request(&request);
        let mut flags = 0;
        if result.exception.is_some() {
            flags |= RESULT_EXCEPTION;
        }
        if result.should_block() {
            flags |= RESULT_BLOCKED;
        }
        if result.important {
            flags |= RESULT_IMPORTANT;
        }

        // Report which enabled category independently matched. The combined
        // result above remains authoritative so cross-list exceptions work.
        if category_mask & CATEGORY_ADS != 0
            && engine.ads.check_network_request(&request).should_block()
        {
            flags |= RESULT_ADS;
        }
        if category_mask & CATEGORY_TRACKERS != 0
            && engine
                .trackers
                .check_network_request(&request)
                .should_block()
        {
            flags |= RESULT_TRACKERS;
        }
        flags
    }))
    .unwrap_or(RESULT_ERROR)
}

#[no_mangle]
pub unsafe extern "C" fn island_adblock_engine_ads_rule_count(
    engine: *const IslandAdblockEngine,
) -> u32 {
    engine.as_ref().map_or(0, |engine| engine.ads_rules)
}

#[no_mangle]
pub unsafe extern "C" fn island_adblock_engine_tracker_rule_count(
    engine: *const IslandAdblockEngine,
) -> u32 {
    engine.as_ref().map_or(0, |engine| engine.tracker_rules)
}

#[no_mangle]
pub unsafe extern "C" fn island_adblock_engine_destroy(engine: *mut IslandAdblockEngine) {
    if !engine.is_null() {
        drop(Box::from_raw(engine));
    }
}
