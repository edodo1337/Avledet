#include "ZDOID.h"

const ZDOID ZDOID::NONE = ZDOID();

std::ostream &operator<<(std::ostream &st, ZDOID const &zdoid)
{
    st << zdoid.get_user_id() << ":" << zdoid.get_id();
    return st;
}