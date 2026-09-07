// Tracks Gemini API calls against a daily budget, shared across every call
// site (image analysis + sensor-only checks both draw from the same
// per-model daily quota on Google's side). Once the budget is used up for
// the day, callers should skip the network call entirely instead of hitting
// a 429 - cheaper, faster, and doesn't spam the logs with rate-limit errors.
//
// This is an approximate 24h rolling window from server start, not aligned
// to Google's actual reset time (which isn't published) - close enough for
// a hobby deployment to avoid hammering the API once the real quota is gone.

const envLimit = Number(process.env.GEMINI_DAILY_LIMIT);
const DAILY_LIMIT = Number.isFinite(envLimit) && envLimit >= 0 ? envLimit : 20;
const DAY_MS = 24 * 60 * 60 * 1000;

let windowStart = Date.now();
let used = 0;

function resetIfNewWindow() {
  const now = Date.now();
  if (now - windowStart >= DAY_MS) {
    windowStart = now;
    used = 0;
  }
}

/** Call before making a Gemini request. Returns false if the daily budget is already spent. */
function tryConsume() {
  resetIfNewWindow();
  if (used >= DAILY_LIMIT) return false;
  used += 1;
  return true;
}

function getStatus() {
  resetIfNewWindow();
  return { used, limit: DAILY_LIMIT, remaining: Math.max(DAILY_LIMIT - used, 0) };
}

module.exports = { tryConsume, getStatus };
