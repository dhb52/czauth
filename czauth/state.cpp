#include "State.h"

InitState InitState::_state;
NetReadyState NetReadyState::_state;
AuthOkState AuthOkState::_state;
AuthErrorState AuthErrorState::_state;

void InitState::NetUp(StateContext& context)
{
	//TODO

	context.ChangeState(NetReadyState::Instance());
}

void InitState::AuthOk(StateContext& context)
{
}

void InitState::AuthError(StateContext& context)
{
}