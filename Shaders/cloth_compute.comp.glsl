/**
 * @file cloth_compute.comp.glsl
 * @brief GPU-based cloth simulation using Position-Based Dynamics (PBD)
 * 
 * This compute shader implements a Verlet integration scheme with distance
 * constraints for realistic cloth simulation. The algorithm follows:
 * 1. Apply external forces (gravity, wind)
 * 2. Verlet integration for position prediction
 * 3. Iterative constraint solving (Gauss-Seidel relaxation)
 * 4. Velocity damping for stability
 * 5. Normal recalculation for rendering
 */

#version 460

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ============================================================================
// Data Structures (std430 layout for SSBOs)
// ============================================================================

/**
 * Particle representation for cloth vertices.
 * Using explicit padding to ensure proper GPU memory alignment.
 * inverseMass of 0.0 indicates a pinned/fixed particle.
 */
struct Particle {
    vec3 position;
    float _pad0;            // Padding for 16-byte alignment
    vec3 previousPosition;
    float _pad1;            // Padding for 16-byte alignment
    vec3 velocity;
    float inverseMass;      // 0.0 = pinned particle (infinite mass)
};

/**
 * Distance constraint between two particles.
 * Used to maintain structural, shear, and bend connections.
 */
struct Constraint {
    uint particleA;         // Index of first particle
    uint particleB;         // Index of second particle
    float restLength;       // Target distance between particles
    float stiffness;        // Constraint stiffness [0.0, 1.0]
};

// ============================================================================
// Shader Storage Buffer Objects (SSBOs)
// ============================================================================

// Particle buffer - read/write access for simulation updates
layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

// Constraint buffer - read-only, defines cloth structure
layout(std430, binding = 1) readonly buffer ConstraintBuffer {
    Constraint constraints[];
};

// Output normals for rendering - computed after simulation step
layout(std430, binding = 2) writeonly buffer NormalBuffer {
    vec4 normals[];         // vec4 for alignment, w unused
};

// ============================================================================
// Uniform Buffer Object - Simulation Parameters
// ============================================================================

layout(std140, binding = 0) uniform SimulationParams {
    float deltaTime;        // Time step (typically 1/60 or smaller)
    float damping;          // Velocity damping factor [0.0, 1.0]
    uint numParticles;      // Total particle count
    uint numConstraints;    // Total constraint count
    
    vec3 gravity;           // Gravity acceleration (e.g., vec3(0, -9.81, 0))
    uint solverIterations;  // PBD solver iterations (higher = stiffer cloth)
    
    vec3 windVelocity;      // Wind direction and magnitude
    float _pad0;            // Padding for std140 alignment
};

// ============================================================================
// Shared Memory for Workgroup Synchronization
// ============================================================================

// Shared memory can be used for local constraint solving optimization
shared vec3 sharedPositions[64];

// ============================================================================
// Function Implementations
// ============================================================================

/**
 * Applies external forces to a particle.
 * 
 * Forces applied:
 * - Gravity: Constant downward acceleration
 * - Wind: Simplified aerodynamic drag based on velocity difference
 * 
 * @param p The particle to modify (in/out parameter)
 */
void applyExternalForces(inout Particle p) {
    // Skip pinned particles (inverseMass == 0.0)
    if (p.inverseMass <= 0.0) {
        return;
    }
    
    // Apply gravity: F = m * g, a = F * inverseMass = g
    p.velocity += gravity * deltaTime;
    
    // Apply wind force using simplified drag model
    // Wind force is proportional to relative velocity difference
    vec3 relativeVelocity = windVelocity - p.velocity;
    float windStrength = length(relativeVelocity);
    
    if (windStrength > 0.001) {
        // Simplified aerodynamic drag: F_drag = 0.5 * rho * Cd * A * v^2
        // We combine constants into a single drag coefficient
        const float dragCoefficient = 0.01;
        vec3 windForce = normalize(relativeVelocity) * windStrength * windStrength * dragCoefficient;
        p.velocity += windForce * p.inverseMass * deltaTime;
    }
}

/**
 * Performs Verlet integration to update particle position.
 * 
 * Verlet integration provides better stability than Euler for
 * constraint-based simulations. Formula:
 * x(t+dt) = 2*x(t) - x(t-dt) + a*dt^2
 * 
 * Equivalent velocity form used here:
 * x(t+dt) = x(t) + v*dt
 * 
 * @param p The particle to integrate (in/out parameter)
 */
void verletIntegration(inout Particle p) {
    // Skip pinned particles
    if (p.inverseMass <= 0.0) {
        return;
    }
    
    // Store current position for next frame's velocity calculation
    vec3 currentPosition = p.position;
    
    // Verlet integration: new_pos = pos + velocity * dt
    // Velocity is implicitly: v = (pos - prev_pos) / dt
    p.position = p.position + p.velocity * deltaTime;
    
    // Update previous position for next iteration
    p.previousPosition = currentPosition;
}

/**
 * Solves a single distance constraint using Position-Based Dynamics.
 * 
 * The constraint maintains a fixed rest length between two particles.
 * Uses weighted projection based on inverse masses for proper mass handling.
 * 
 * @param constraintIndex Index into the constraint buffer
 */
void solveDistanceConstraint(uint constraintIndex) {
    Constraint c = constraints[constraintIndex];
    
    // Get particle references
    uint idxA = c.particleA;
    uint idxB = c.particleB;
    
    // Bounds checking
    if (idxA >= numParticles || idxB >= numParticles) {
        return;
    }
    
    vec3 posA = particles[idxA].position;
    vec3 posB = particles[idxB].position;
    float invMassA = particles[idxA].inverseMass;
    float invMassB = particles[idxB].inverseMass;
    
    // Calculate constraint vector
    vec3 delta = posB - posA;
    float currentLength = length(delta);
    
    // Avoid division by zero for degenerate case
    if (currentLength < 0.0001) {
        return;
    }
    
    // Calculate total inverse mass (sum of both particles)
    float totalInvMass = invMassA + invMassB;
    
    // Skip if both particles are pinned
    if (totalInvMass <= 0.0) {
        return;
    }
    
    // Calculate constraint error (how much the length differs from rest length)
    float error = currentLength - c.restLength;
    
    // Normalized correction direction
    vec3 correction = (delta / currentLength) * error * c.stiffness;
    
    // Apply weighted corrections based on inverse mass ratio
    // Lighter particles move more, heavier particles move less
    float weightA = invMassA / totalInvMass;
    float weightB = invMassB / totalInvMass;
    
    // Update positions with corrections
    particles[idxA].position += correction * weightA;
    particles[idxB].position -= correction * weightB;
}

/**
 * Applies velocity damping for simulation stability.
 * 
 * Damping reduces oscillation and provides energy dissipation,
 * simulating air resistance and internal friction.
 * 
 * @param p The particle to damp (in/out parameter)
 */
void applyDamping(inout Particle p) {
    // Skip pinned particles
    if (p.inverseMass <= 0.0) {
        return;
    }
    
    // Recalculate velocity from position change (Verlet-style)
    p.velocity = (p.position - p.previousPosition) / deltaTime;
    
    // Apply damping: reduce velocity by damping factor
    // damping of 0.99 means 1% energy loss per frame
    p.velocity *= damping;
}

/**
 * Calculates the vertex normal for a particle based on adjacent faces.
 * 
 * Assumes a regular grid topology where each particle (except edges)
 * has up to 4 neighboring quads. Uses cross products to compute
 * face normals and averages them.
 * 
 * @param particleIndex Index of the particle
 * @return Normalized vertex normal vector
 */
vec3 calculateNormal(uint particleIndex) {
    // For cloth simulation, we typically have a grid layout
    // This implementation assumes neighbors are at predictable offsets
    // Adjust gridWidth based on your cloth mesh topology
    
    vec3 normal = vec3(0.0);
    vec3 center = particles[particleIndex].position;
    
    // Calculate grid dimensions (assuming square grid for simplicity)
    // In production, pass these as uniforms
    uint gridWidth = uint(sqrt(float(numParticles)));
    
    uint x = particleIndex % gridWidth;
    uint y = particleIndex / gridWidth;
    
    // Check bounds and calculate normals from adjacent triangles
    // Using 4-connectivity for quad-based mesh
    
    // Right neighbor
    vec3 right = (x + 1 < gridWidth) ? 
        particles[particleIndex + 1].position : center;
    
    // Left neighbor  
    vec3 left = (x > 0) ? 
        particles[particleIndex - 1].position : center;
    
    // Up neighbor
    vec3 up = (y + 1 < gridWidth) ? 
        particles[particleIndex + gridWidth].position : center;
    
    // Down neighbor
    vec3 down = (y > 0) ? 
        particles[particleIndex - gridWidth].position : center;
    
    // Calculate normals from 4 adjacent triangles and accumulate
    // Triangle 1: center, right, up
    if (x + 1 < gridWidth && y + 1 < gridWidth) {
        vec3 e1 = right - center;
        vec3 e2 = up - center;
        normal += cross(e1, e2);
    }
    
    // Triangle 2: center, up, left
    if (x > 0 && y + 1 < gridWidth) {
        vec3 e1 = up - center;
        vec3 e2 = left - center;
        normal += cross(e1, e2);
    }
    
    // Triangle 3: center, left, down
    if (x > 0 && y > 0) {
        vec3 e1 = left - center;
        vec3 e2 = down - center;
        normal += cross(e1, e2);
    }
    
    // Triangle 4: center, down, right
    if (x + 1 < gridWidth && y > 0) {
        vec3 e1 = down - center;
        vec3 e2 = right - center;
        normal += cross(e1, e2);
    }
    
    // Normalize the accumulated normal
    float len = length(normal);
    return (len > 0.0001) ? normal / len : vec3(0.0, 1.0, 0.0);
}

// ============================================================================
// Main Compute Shader Entry Point
// ============================================================================

void main() {
    uint globalId = gl_GlobalInvocationID.x;
    
    // ========================================================================
    // Phase 1: Apply External Forces and Integrate
    // ========================================================================
    
    if (globalId < numParticles) {
        Particle p = particles[globalId];
        
        // Apply external forces (gravity, wind)
        applyExternalForces(p);
        
        // Verlet integration to predict new position
        verletIntegration(p);
        
        // Write back to buffer
        particles[globalId] = p;
    }
    
    // Synchronize all invocations before constraint solving
    memoryBarrierBuffer();
    barrier();
    
    // ========================================================================
    // Phase 2: Iterative Constraint Solving (Gauss-Seidel)
    // ========================================================================
    
    for (uint iteration = 0; iteration < solverIterations; ++iteration) {
        // Each invocation processes a subset of constraints
        // Using strided access pattern for better cache utilization
        uint constraintsPerInvocation = (numConstraints + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
        
        for (uint i = 0; i < constraintsPerInvocation; ++i) {
            uint constraintIdx = globalId + i * gl_WorkGroupSize.x;
            
            if (constraintIdx < numConstraints) {
                solveDistanceConstraint(constraintIdx);
            }
        }
        
        // Synchronize between solver iterations
        // Critical for convergence of iterative solver
        memoryBarrierBuffer();
        barrier();
    }
    
    // ========================================================================
    // Phase 3: Apply Damping and Calculate Normals
    // ========================================================================
    
    if (globalId < numParticles) {
        Particle p = particles[globalId];
        
        // Apply velocity damping for stability
        applyDamping(p);
        
        // Write final particle state
        particles[globalId] = p;
        
        // Calculate and store vertex normal for rendering
        vec3 normal = calculateNormal(globalId);
        normals[globalId] = vec4(normal, 0.0);
    }
}
