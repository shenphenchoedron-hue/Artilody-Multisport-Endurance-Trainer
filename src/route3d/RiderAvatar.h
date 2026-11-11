#pragma once

#include "glb_model.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace watt {

// The athlete's look in the 3D views, chosen in the user profile
// (ProfileManager::avatar3d / runner3d / rower3d, the same keys): which
// textured figure - "male" or "female" (rider_male.glb, ... /
// runner_male.glb, ... / rower_male.glb, ...) - and the
// colours of the avatar's jersey (a runner's / rower's top), shorts, helmet
// (cyclists), shoes and hair, the bike's frame and the boat's hull
// ("#rrggbb"; "" = the model's own).
//
// The figures are loaded once per process, in the background (a few tenths
// of a second each): the views ask with figureIfReady() and draw what they
// have until it is there; preload() starts the load early and says when it
// is done.
struct RiderAvatar
{
    enum Kind { Cyclist, Runner, Rower };
    Kind kind = Cyclist;
    QString model = QStringLiteral("male");
    route3d::GlbModel::GarmentColors colors;
    // the bike frame's colour ("bike"; a cyclist, any figure); x < 0: the standard blue
    QVector3D bikeColor { -1, 0, 0 };
    // the boat's hull colour ("boat"; a rower, any figure); x < 0: the standard red
    QVector3D boatColor { -1, 0, 0 };

    static RiderAvatar fromVariant(const QVariantMap &values, Kind kind = Cyclist);
    bool operator==(const RiderAvatar &o) const;
    bool operator!=(const RiderAvatar &o) const { return !(*this == o); }

    // The figure (the other one of its kind when the chosen model file is
    // not there); null when none can be loaded. A cyclist or a rower can be
    // posed (GlbModel::canRide()); a runner is animated. Waits for the load.
    std::shared_ptr<const route3d::GlbModel> figure() const;
    // The same without waiting: null and *pending = true while it loads (the
    // load is started).
    std::shared_ptr<const route3d::GlbModel> figureIfReady(bool *pending) const;
    // A runner walking: the same figure with the walking motion (walker.glb)
    // (null: none). Waiting / not waiting as above.
    std::shared_ptr<const route3d::GlbModel> walkingFigure() const;
    std::shared_ptr<const route3d::GlbModel> walkingFigureIfReady(bool *pending) const;
    // Starts loading the figure (and a runner's walking figure); `done` is
    // called when both are there (on a worker thread - post to your own;
    // at once when they are loaded already).
    void preload(std::function<void()> done = {}) const;
    // Its vertex colours in the chosen colours (empty: the model's own).
    std::vector<QVector3D> figureColors(const route3d::GlbModel &figure) const;

    // The figures to choose from: { id, name } for those that are there.
    static QVariantList models(Kind kind = Cyclist);
    // The keys of the garments in the profile's map, in Garment order (1..).
    static const char *garmentKey(route3d::GlbModel::Garment g);
};

} // namespace watt
