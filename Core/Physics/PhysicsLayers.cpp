#include "Core/Physics/PhysicsLayers.h"

namespace Core {
namespace Physics {

    // BroadPhaseLayerInterfaceImpl
    BroadPhaseLayerInterfaceImpl::BroadPhaseLayerInterfaceImpl()
    {
        // Map object layers to broadphase layers
        m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        m_ObjectToBroadPhase[Layers::DEBRIS] = BroadPhaseLayers::DEBRIS;
        m_ObjectToBroadPhase[Layers::SENSOR] = BroadPhaseLayers::SENSOR;
    }

    uint32_t BroadPhaseLayerInterfaceImpl::GetNumBroadPhaseLayers() const
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_ObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING): return "NON_MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):     return "MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::DEBRIS):     return "DEBRIS";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::SENSOR):     return "SENSOR";
            default: return "UNKNOWN";
        }
    }
#endif

    // ObjectLayerPairFilterImpl
    bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const
    {
        switch (inObject1) {
            case Layers::NON_MOVING:
                // Non-moving objects only collide with moving objects and debris
                return inObject2 == Layers::MOVING || inObject2 == Layers::DEBRIS;
            
            case Layers::MOVING:
                // Moving objects collide with everything except sensors
                return inObject2 != Layers::SENSOR;
            
            case Layers::DEBRIS:
                // Debris collides with non-moving and moving, but not other debris or sensors
                return inObject2 == Layers::NON_MOVING || inObject2 == Layers::MOVING;
            
            case Layers::SENSOR:
                // Sensors only detect moving objects (trigger-like behavior)
                return inObject2 == Layers::MOVING;
            
            default:
                JPH_ASSERT(false);
                return false;
        }
    }

    // ObjectVsBroadPhaseLayerFilterImpl
    bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const
    {
        switch (inLayer1) {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING || inLayer2 == BroadPhaseLayers::DEBRIS;
            
            case Layers::MOVING:
                return inLayer2 != BroadPhaseLayers::SENSOR;
            
            case Layers::DEBRIS:
                return inLayer2 == BroadPhaseLayers::NON_MOVING || inLayer2 == BroadPhaseLayers::MOVING;
            
            case Layers::SENSOR:
                return inLayer2 == BroadPhaseLayers::MOVING;
            
            default:
                JPH_ASSERT(false);
                return false;
        }
    }

} // namespace Physics
} // namespace Core
