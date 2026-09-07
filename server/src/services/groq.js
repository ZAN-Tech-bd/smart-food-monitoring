const Groq = require('groq-sdk');

const apiKey = process.env.GROQ_API_KEY;
const modelName = process.env.GROQ_MODEL || 'openai/gpt-oss-20b';

let client = null;
function getClient() {
  if (!apiKey) return null;
  if (!client) client = new Groq({ apiKey });
  return client;
}

function buildSensorOnlyPrompt(sensorSnapshot) {
  const { temperature, humidity, gas_raw, weight_g } = sensorSnapshot;
  const gasWarning = Number(process.env.GAS_WARNING_THRESHOLD) || 1500;
  const gasDanger = Number(process.env.GAS_DANGER_THRESHOLD) || 2800;
  return `You are a food safety assistant monitoring an unattended food storage unit. No camera photo is available right now, so base your assessment only on these sensor readings:
- Temperature: ${temperature ?? 'unknown'} C
- Humidity: ${humidity ?? 'unknown'} %
- Gas/smoke sensor (raw analog, higher = more gas detected): ${gas_raw ?? 'unknown'} - this sensor's own clean-air baseline is calibrated so that below ${gasWarning} is Normal, ${gasWarning}-${gasDanger} is Warning, and above ${gasDanger} is Danger. Judge the gas reading against THESE thresholds, not generic assumptions about raw ADC values.
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
      return { verdict: parsed.verdict, notes: parsed.notes || '', food: null, raw: text };
    }
  } catch (err) {
    // fall through to the Unknown case below
  }
  return { verdict: 'Unknown', notes: text.slice(0, 500), food: null, raw: text };
}

/**
 * Analyze sensor readings alone with Groq (text-only, no vision needed).
 * Same never-throws contract as the Gemini functions - returns "Unknown" on
 * any failure so callers can still write a row to the database. Groq's free
 * tier has a much higher daily request limit than Gemini's, so this is used
 * for the frequent sensor-only checks, leaving Gemini's tight quota free for
 * image analysis.
 */
async function analyzeSensorsOnly(sensorSnapshot) {
  const groq = getClient();
  if (!groq) {
    return { verdict: 'Unknown', notes: 'GROQ_API_KEY is not configured on the server.', food: null, raw: null };
  }

  try {
    const prompt = buildSensorOnlyPrompt(sensorSnapshot);
    const completion = await groq.chat.completions.create({
      model: modelName,
      messages: [{ role: 'user', content: prompt }],
      temperature: 0.3,
    });

    const text = completion.choices?.[0]?.message?.content || '';
    return parseResponseText(text);
  } catch (err) {
    console.error('Groq sensor-only analysis failed:', err.message);
    return { verdict: 'Unknown', notes: `Analysis failed: ${err.message}`, food: null, raw: null };
  }
}

module.exports = { analyzeSensorsOnly, buildSensorOnlyPrompt };
