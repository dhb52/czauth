#pragma once


class noncopyable
{
protected:
	noncopyable() {}
	~noncopyable() {}
private:
	noncopyable(const noncopyable&);
	const noncopyable& operator = (const noncopyable&);
};

class StateContext;

class State : public noncopyable
{
public:
	virtual void NetUp(StateContext&) {}
	virtual void NetDown(StateContext&);
	virtual void AuthOk(StateContext&) {}
	virtual void AuthError(StateContext&) {}
};

/*
 * 0. InitState 
 * 1. NetReadyState
 * 2. AuthOkState
 * 3. AuthErrorState
 * 
 */

class InitState : public State
{
public:
	static State* Instance() { return &_state; }

	virtual void NetUp(StateContext&);
	virtual void NetDown(StateContext&);
	virtual void AuthOk(StateContext&);
	virtual void AuthError(StateContext&);

private:
	InitState() {}
	static InitState _state;
};


class NetReadyState : public State
{
public:
	static State* Instance() { return &_state; }

	virtual void NetUp(StateContext& context);
	virtual void NetDown(StateContext&);
	virtual void AuthOk(StateContext&);
	virtual void AuthError(StateContext&);

private:
	NetReadyState() {}
	static NetReadyState _state;
};

class AuthOkState : public State
{
public:
	static State* Instance() { return &_state; }

	virtual void NetUp(StateContext& context);
	virtual void NetDown(StateContext&);
	virtual void AuthOk(StateContext&);
	virtual void AuthError(StateContext&);

private:
	AuthOkState() {}
	static AuthOkState _state;
};

class AuthErrorState : public State
{
public:
	static State* Instance() { return &_state; }

	virtual void NetUp(StateContext& context);
	virtual void NetDown(StateContext&);
	virtual void AuthOk(StateContext&);
	virtual void AuthError(StateContext&);

private:
	AuthErrorState() {}
	static AuthErrorState _state;
};



/* 1. NetUp
 * 2. NetDown
 * 3. AuthOk
 * 4. AuthError
 * 5. 
 */



class StateContext
{
public:
	StateContext();
	void NetUp() { _state->NetUp(*this); }
	void NetDown() { _state->NetDown(*this); }
	void AuthOk() { _state->AuthOk(*this); }
	void AuthError() { _state->AuthError(*this); }

	void ChangeState(State* newState) { _state = newState; }
private:
	State* _state;
};


