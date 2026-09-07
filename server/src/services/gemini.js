const fs = require('fs');
const { GoogleGenerativeAI } = require('@google/generative-ai');
const budget = require('./geminiBudget');

const apiKey = process.env.GEMINI_API_KEY;
const modelName = process.env.GEMINI_MODEL || 'gemini-3.6-flash';

function budgetExhaustedResult() {
  const { used, limit } = budget.getStatus();
  return {
    verdict: 'Unknown',
    notes: `Daily Gemini quota reached (${used}/${limit} used) - will resume analyzing after it resets.`,
    food: null,
    raw: null,
  };
}

let client = null;
function getClient() {
  if (!apiKey) return null;
  if (!client) client = new GoogleGenerativeAI(apiKey);
  return client;
}

function buildPrompt(sensorSnapshot) {
  const { temperature, humidity, gas_raw, weight_g } = sensorSnapshot;
  return `You are a food safety assistant analyzing a photo from an unattended food storage monitor.

Latest sensor readings at the time this photo was taken:
- Temperature: ${temperature ?? 'unknown'} C
- Humidity: ${humidity ?? 'unknown'} %
- Gas/smoke sensor (raw analog, higher = more gas detected): ${gas_raw ?? 'unknown'}
- Weight on the load cell: ${weight_g ?? 'unknown'} g

Identify the food item visible in the photo, and look for visible signs of spoilage (mold, discoloration, wilting, liquid/mush, pests).

Your verdict must weigh BOTH sources of evidence together, not just one:
- The visual condition of the food in the photo.
- The sensor readings above (e.g. a food that looks fine visually should still be marked down if temperature/gas readings suggest a real risk, and a food with early visual aging should be marked down further if conditions are also poor).
Your notes should mention whichever of the two (or both) actually drove the verdict.

Respond with ONLY a compact JSON object, no markdown fences, no extra text, in exactly this shape:
{"food": "short name of the food item, or \\"Unknown\\" if none is identifiable", "verdict": "Fresh" | "Good" | "Caution" | "Spoiling" | "Spoiled" | "Unclear", "notes": "one or two short sentences explaining why"}

Verdict guide:
- Fresh: looks newly stored, no signs of aging at all
- Good: still perfectly fine to eat, nothing concerning
- Caution: early signs of aging, best consumed soon
- Spoiling: visible spoilage starting (soft spots, discoloration, early mold)
- Spoiled: clearly spoiled or unsafe to eat
- Unclear: can't make a reliable call from the image (bad lighting, no food visible, obstructed view)`;
}

function buildSensorOnlyPrompt(sensorSnapshot) {
  const { temperature, humidity, gas_raw, weight_g } = sensorSnapshot;
  return `You are a food safety assistant monitoring an unattended food storage unit. No camera photo is available right now, so base your assessment only on these sensor readings:
- Temperature: ${temperature ?? 'unknown'} C
- Humidity: ${humidity ?? 'unknown'} %
- Gas/smoke sensor (raw analog, higher = more gas detected): ${gas_raw ?? 'unknown'}
- Weight on the load cell: ${weight_g ?? 'unknown'} g

Judge whether these readings look like normal, safe food storage conditions.

Respond with ONLY a compact JSON object, no markdown fences, no extra text, in exactly this shape:
{"verdict": "Good" | "Caution" | "Not Good", "notes": "one short sentence explaining why"}

Verdict guide:
- Good: readings look like normal, safe storage conditions
- Caution: one or more readings are borderline and worth watching
- Not Good: readings indicate a real problem (e.g. high gas, unsafe temperature)`;
}

function parseResponseText(text) {
  const trimmed = text.trim().replace(/^```json/i, '').replace(/^```/, '').replace(/```$/, '').trim();
  try {
    const parsed = JSON.parse(trimmed);
    if (parsed && typeof parsed.verdict === 'string') {
      return {
        verdict: parsed.verdict,
        notes: parsed.notes || '',
        food: typeof parsed.food === 'string' ? parsed.food : null,
        raw: text,
      };
    }
  } catch (err) {
    // fall through to the Unknown case below
  }
  return { verdict: 'Unknown', notes: text.slice(0, 500), food: null, raw: text };
}

/**
 * Analyze a food image with Gemini, given a snapshot of sensor readings taken
 * around the same time. Returns { verdict, notes, raw } — never throws; on
 * any failure it returns a verdict of "Unknown" so callers can still write a
 * row to the database.
 */
async function analyzeImage(imagePath, sensorSnapshot) {
  const genAI = getClient();
  if (!genAI) {
    return { verdict: 'Unknown', notes: 'GEMINI_API_KEY is not configured on the server.', food: null, raw: null };
  }
  if (!budget.tryConsume()) {
    return budgetExhaustedResult();
  }

  try {
    const model = genAI.getGenerativeModel({ model: modelName });
    const imageBytes = fs.readFileSync(imagePath);
    const prompt = buildPrompt(sensorSnapshot);

    const result = await model.generateContent([
      { text: prompt },
      { inlineData: { mimeType: 'image/jpeg', data: imageBytes.toString('base64') } },
    ]);

    const text = result.response.text();
    return parseResponseText(text);
  } catch (err) {
    console.error('Gemini analysis failed:', err.message);
    return { verdict: 'Unknown', notes: `Analysis failed: ${err.message}`, food: null, raw: null };
  }
}

/**
 * Analyze sensor readings alone with Gemini, for when no photo is available
 * yet (e.g. between camera captures, or if the camera node isn't set up).
 * Same never-throws contract as analyzeImage - returns "Unknown" on failure.
 */
async function analyzeSensorsOnly(sensorSnapshot) {
  const genAI = getClient();
  if (!genAI) {
    return { verdict: 'Unknown', notes: 'GEMINI_API_KEY is not configured on the server.', food: null, raw: null };
  }
  if (!budget.tryConsume()) {
    return budgetExhaustedResult();
  }

  try {
    const model = genAI.getGenerativeModel({ model: modelName });
    const prompt = buildSensorOnlyPrompt(sensorSnapshot);

    const result = await model.generateContent([{ text: prompt }]);

    const text = result.response.text();
    return parseResponseText(text);
  } catch (err) {
    console.error('Gemini sensor-only analysis failed:', err.message);
    return { verdict: 'Unknown', notes: `Analysis failed: ${err.message}`, raw: null };
  }
}

module.exports = { analyzeImage, analyzeSensorsOnly, buildPrompt, buildSensorOnlyPrompt };
