#pragma once

#include "world.hpp"
#include "agent_gateway.hpp"
#include "entity_registry.hpp"


class Arena {
    public:

    private:
    AgentGateway gateway_;
    EntityRegistry registry_;
};