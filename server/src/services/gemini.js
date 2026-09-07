const fs = require('fs');
const { GoogleGenerativeAI } = require('@google/generative-ai');

const apiKey = process.env.GEMINI_API_KEY;
const modelName = process.env.GEMINI_MODEL || 'gemini-3.6-flash';

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

Look at the image for visible signs of spoilage (mold, discoloration, wilting, liquid/mush, pests) and combine that with the sensor readings.

Respond with ONLY a compact JSON object, no markdown fences, no extra text, in exactly this shape:
{"verdict": "Fresh" | "Caution" | "Spoiled" | "Unclear", "notes": "one or two short sentences explaining why"}`;
}

function buildSensorOnlyPrompt(sensorSnapshot) {
  const { temperature, humidity, gas_raw, weight_g } = sensorSnapshot;
  return `You are a food safety assistant monitoring an unattended food storage unit. No camera photo is available right now, so base your assessment only on these sensor readings:
- Temperature: ${temperature ?? 'unknown'} C
- Humidity: ${humidity ?? 'unknown'} %
- Gas/smoke sensor (raw analog, higher = more gas detected): ${gas_raw ?? 'unknown'}
- Weight on the load cell: ${weight_g ?? 'unknown'} g

Judge whether these readings look like normal safe food storage conditions or not.

Respond with ONLY a compact JSON object, no markdown fences, no extra text, in exactly this shape:
{"verdict": "Good" | "Not Good", "notes": "one short sentence explaining why"}`;
}

function parseResponseText(text) {
  const trimmed = text.trim().replace(/^```json/i, '').replace(/^```/, '').replace(/```$/, '').trim();
  try {
    const parsed = JSON.parse(trimmed);
    if (parsed && typeof parsed.verdict === 'string') {
      return { verdict: parsed.verdict, notes: parsed.notes || '', raw: text };
    }
  } catch (err) {
    // fall through to the Unknown case below
  }
  return { verdict: 'Unknown', notes: text.slice(0, 500), raw: text };
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
    return { verdict: 'Unknown', notes: 'GEMINI_API_KEY is not configured on the server.', raw: null };
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
    return { verdict: 'Unknown', notes: `Analysis failed: ${err.message}`, raw: null };
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
    return { verdict: 'Unknown', notes: 'GEMINI_API_KEY is not configured on the server.', raw: null };
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
