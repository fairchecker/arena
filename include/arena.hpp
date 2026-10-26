#include "world.hpp"
#include "agent_gateway.hpp"
#include <vector>

#pragma once


class Arena {
    public:

    private:
    AgentGateway gateway_;
    World world_;

};

struct Position {
    int row;
    int column;
};

enum Rotation {
    Up,
    Right,
    Down,
    Left
};