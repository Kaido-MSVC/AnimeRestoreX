#include "core/Progress.h"

#include <algorithm>
#include <cmath>

namespace arx {

namespace {

constexpr double kMinMegapixelsToLearn = 0.01;  // tiny images measure only overhead
constexpr double kSmoothing = 0.5;              // weight of the newest measurement

// Estimated fraction of a stage that is expected to take `expected` seconds
// and has run `elapsed` seconds. Linear up to 90%, then it slows down and
// never reaches 100% before the stage really ends.
double estimatedFraction(double elapsed, double expected)
{
    if (expected <= 0)
        return 0.99;
    const double x = elapsed / expected;
    if (x <= 0.9)
        return std::max(0.0, x);
    return 0.9 + 0.09 * (1.0 - std::exp(-(x - 0.9) / 0.5));
}

} // namespace

bool ProgressEstimator::isValidRate(double r)
{
    return std::isfinite(r) && r > 0 && r <= 1000;
}

void ProgressEstimator::setLearnedRates(const QHash<QString, double> &rates)
{
    m_learned.clear();
    for (auto it = rates.cbegin(); it != rates.cend(); ++it)
        if (!it.key().isEmpty() && isValidRate(it.value()))
            m_learned.insert(it.key(), it.value());
}

double ProgressEstimator::machineFactor() const
{
    // How much faster or slower this machine is than the default inference
    // rates, from the inference keys that were measured (geometric mean).
    double logSum = 0;
    int n = 0;
    for (auto it = m_defaults.cbegin(); it != m_defaults.cend(); ++it) {
        const auto learned = m_learned.constFind(it.key());
        if (learned != m_learned.cend() && it.value() > 0) {
            logSum += std::log(*learned / it.value());
            ++n;
        }
    }
    return n > 0 ? std::exp(logSum / n) : 1.0;
}

bool ProgressEstimator::hasMeasuredInference() const
{
    for (auto it = m_defaults.cbegin(); it != m_defaults.cend(); ++it)
        if (m_learned.contains(it.key()))
            return true;
    return false;
}

double ProgressEstimator::rateFor(const StagePlan &s) const
{
    if (const auto it = m_learned.constFind(s.rateKey); it != m_learned.cend())
        return *it;
    // An inference model not measured yet: scale its default by how fast the
    // measured models ran on this machine.
    if (s.stage == Stage::Inference)
        return s.defaultRate * machineFactor();
    return s.defaultRate;
}

double ProgressEstimator::overheadFor(const StagePlan &s) const
{
    if (!s.overheadKey.isEmpty())
        if (const auto it = m_learned.constFind(s.overheadKey); it != m_learned.cend())
            return *it;
    return s.overheadSec;
}

double ProgressEstimator::stageSeconds(const StagePlan &s) const
{
    return overheadFor(s) + rateFor(s) * s.megapixels;
}

void ProgressEstimator::startBatch(const QList<ItemPlan> &items, qint64 nowMs)
{
    m_items = items;
    m_index = -1;
    m_next = 0;
    m_stage = -1;
    m_stageStart = nowMs;
    m_engineFraction = -1;
    m_doneWeight = 0;
    m_itemStageSec.clear();
    m_itemTotalSec = 0;

    for (const ItemPlan &item : items)
        for (const StagePlan &s : item.stages)
            if (s.stage == Stage::Inference && !s.rateKey.isEmpty())
                m_defaults.insert(s.rateKey, s.defaultRate);

    // Items whose size could not be read count as an average item.
    double known = 0;
    int knownCount = 0;
    for (const ItemPlan &item : items) {
        if (item.weight > 0) {
            known += item.weight;
            ++knownCount;
        }
    }
    const double fallback = knownCount > 0 ? known / knownCount : 1.0;
    m_weights.clear();
    m_totalWeight = 0;
    for (const ItemPlan &item : items) {
        const double w = item.weight > 0 ? item.weight : fallback;
        m_weights << w;
        m_totalWeight += w;
    }
}

void ProgressEstimator::itemStarted(int index, qint64 nowMs)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_index = index;
    m_next = index + 1;
    m_stage = -1;
    m_stageStart = nowMs;
    m_engineFraction = -1;
    // Stage weights are frozen for this item, so its progress never goes back
    // when a measurement changes the speeds.
    m_itemStageSec.clear();
    m_itemTotalSec = 0;
    for (const StagePlan &s : m_items.at(index).stages) {
        const double sec = std::max(0.001, stageSeconds(s));
        m_itemStageSec << sec;
        m_itemTotalSec += sec;
    }
}

void ProgressEstimator::closeStage(qint64 nowMs, bool calibrate)
{
    if (m_index < 0 || m_stage < 0)
        return;
    const StagePlan &s = m_items.at(m_index).stages.at(m_stage);
    if (!calibrate || s.rateKey.isEmpty())
        return;
    const bool canLearnRate = s.megapixels >= kMinMegapixelsToLearn;  // tiny images: overhead only
    if (!canLearnRate && (s.overheadKey.isEmpty() || !m_learned.contains(s.rateKey)))
        return;
    const double elapsed = double(nowMs - m_stageStart) / 1000.0;
    auto learn = [this](const QString &key, double sample) {
        if (!isValidRate(sample))
            return;
        const auto it = m_learned.find(key);
        if (it == m_learned.end())
            m_learned.insert(key, sample);
        else
            *it = (1.0 - kSmoothing) * *it + kSmoothing * sample;
    };
    // Cold stage with a known warm speed: what is left over is the cold-start cost.
    if (!s.overheadKey.isEmpty() && m_learned.contains(s.rateKey)) {
        learn(s.overheadKey, elapsed - m_learned.value(s.rateKey) * s.megapixels);
        return;
    }
    // Warm stage, or a cold one before any warm speed is known (the expected cold-start
    // cost is taken off so the warm speed is not inflated by it).
    learn(s.rateKey, std::max(elapsed - overheadFor(s), 0.0) / s.megapixels);
}

void ProgressEstimator::stageStarted(int index, const StageEvent &e, qint64 nowMs)
{
    if (index != m_index || m_index < 0)
        return;
    const QList<StagePlan> &stages = m_items.at(m_index).stages;
    int found = -1;
    for (int i = std::max(0, m_stage); i < stages.size(); ++i) {
        if (stages.at(i).stage == e.stage && stages.at(i).pass == e.pass) {
            found = i;
            break;
        }
    }
    if (found < 0 || found == m_stage)
        return;
    closeStage(nowMs, true);  // the previous stage ended normally
    m_stage = found;
    m_stageStart = nowMs;
    m_engineFraction = -1;
}

void ProgressEstimator::stageFraction(int index, double fraction, qint64)
{
    if (index != m_index || m_stage < 0 || !std::isfinite(fraction))
        return;
    m_engineFraction = std::max(m_engineFraction, std::clamp(fraction, 0.0, 1.0));
}

void ProgressEstimator::stageRestarted(int index, qint64)
{
    if (index != m_index || m_stage < 0)
        return;
    m_engineFraction = 0;
}

void ProgressEstimator::itemFinished(int index, bool ok, qint64 nowMs)
{
    if (index != m_index || m_index < 0)
        return;
    // A failed or canceled item may have stopped in the middle of a stage.
    closeStage(nowMs, ok);
    m_doneWeight += m_weights.value(index);
    m_index = -1;
    m_stage = -1;
    m_engineFraction = -1;
}

double ProgressEstimator::currentStageFraction(qint64 nowMs) const
{
    if (m_engineFraction >= 0)
        return std::min(m_engineFraction, 0.99);
    const double elapsed = double(nowMs - m_stageStart) / 1000.0;
    return estimatedFraction(elapsed, m_itemStageSec.value(m_stage));
}

ProgressSnapshot ProgressEstimator::snapshot(qint64 nowMs) const
{
    ProgressSnapshot snap;
    snap.index = m_index;
    double itemFraction = 0;
    if (m_index >= 0 && m_stage >= 0 && m_itemTotalSec > 0) {
        const StagePlan &s = m_items.at(m_index).stages.at(m_stage);
        snap.stage = s.stage;
        snap.pass = s.pass;
        int passes = 0;
        for (const StagePlan &p : m_items.at(m_index).stages)
            passes += p.stage == Stage::Inference ? 1 : 0;
        snap.passCount = std::max(1, passes);
        double before = 0;
        for (int i = 0; i < m_stage; ++i)
            before += m_itemStageSec.at(i);
        itemFraction = (before + currentStageFraction(nowMs) * m_itemStageSec.at(m_stage)) / m_itemTotalSec;
        itemFraction = std::clamp(itemFraction, 0.0, 0.99);
    }
    snap.itemFraction = itemFraction;
    if (m_totalWeight > 0) {
        const double current = m_index >= 0 ? itemFraction * m_weights.value(m_index) : 0.0;
        snap.batchFraction = std::clamp((m_doneWeight + current) / m_totalWeight, 0.0, 1.0);
    }

    if (!m_deviceConfirmed || !hasMeasuredInference())
        return snap;  // speeds of another GPU, or only the defaults

    // Remaining time with the current (refined) speeds.
    double remaining = 0;
    if (m_index >= 0) {
        const QList<StagePlan> &stages = m_items.at(m_index).stages;
        int nextStage = 0;
        if (m_stage >= 0) {
            const double elapsed = double(nowMs - m_stageStart) / 1000.0;
            double left = 0;
            if (m_engineFraction > 0.02)
                left = elapsed * (1.0 - m_engineFraction) / m_engineFraction;
            else
                left = stageSeconds(stages.at(m_stage)) - elapsed;
            remaining += std::max(0.0, left);
            nextStage = m_stage + 1;
        }
        for (int i = nextStage; i < stages.size(); ++i)
            remaining += stageSeconds(stages.at(i));
    }
    for (int i = m_next; i < m_items.size(); ++i)
        for (const StagePlan &s : m_items.at(i).stages)
            remaining += stageSeconds(s);
    snap.remainingSec = remaining;
    return snap;
}

} // namespace arx
