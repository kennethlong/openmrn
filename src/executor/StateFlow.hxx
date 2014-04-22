/** \copyright
 * Copyright (c) 2013, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file StateFlow.hxx
 *
 * Defines a type of state machine flow used within class Service.
 *
 * @author Stuart W Baker
 * @date 25 December 2013
 */

#ifndef _StateFlow_hxx_
#define _StateFlow_hxx_

#include <type_traits>

#include "executor/Message.hxx"
#include "executor/Service.hxx"
#include "executor/Timer.hxx"
#include "utils/BufferQueue.hxx"

#define STATE(_fn)                                                             \
    (Callback)(&std::remove_reference<decltype(*this)>::type::_fn)

/** Declare a state callback in a StateFlow.
 * @param _state the method name of the StateFlow state callback
 */
#define STATE_FLOW_STATE(_state) Action _state()

/** Begin the definition of a StateFlow.
 * @param _name the class name of the StateFlow derived object
 * @param _priorities number of input queue priorities
 */
#define STATE_FLOW_START(_name, _message, _priorities)                         \
    class _name : public StateFlow<_message, _priorities>                      \
    {                                                                          \
    public:                                                                    \
        _name(Service *service) : StateFlow<_priorities, _priorities>(service) \
        {                                                                      \
        }                                                                      \
                                                                               \
    private:                                                                   \
        STATE_FLOW_STATE(entry);                                               \
                                                                               \
        _name();                                                               \
                                                                               \
    DISALLOW_COPY_AND_ASSIGN(_name)

/** Begin the definition of a StateFlow that includes timeouts.
 * @param _name the class name of the StateFlow derived object
 * @param _priorities number of input queue priorities
 */
#define STATE_FLOW_START_WITH_TIMER(_name, _priorities)                        \
    class _name : public StateFlow<_priorities>                                \
    {                                                                          \
    public:                                                                    \
        _name(Service *service)                                                \
            : StateFlow<_priorities>(service)                                  \
            , timer(TIMEOUT_FROM(service, state_flow_timeout), service, this)  \
            , timerMsg(NULL)                                                   \
        {                                                                      \
        }                                                                      \
                                                                               \
        ~_name()                                                               \
        {                                                                      \
        }                                                                      \
                                                                               \
        void timeout()                                                         \
        {                                                                      \
            timerMsg ? me()->send(timerMsg) :;                                 \
            timerMsg = NULL;                                                   \
        }                                                                      \
                                                                               \
        void trigger()                                                         \
        {                                                                      \
            timer.trigger();                                                   \
        }                                                                      \
                                                                               \
    private:                                                                   \
        STATE_FLOW_STATE(entry);                                               \
                                                                               \
        Action timeout_and_call(Callback c, Message *msg, long long period)    \
        {                                                                      \
            msg->id(msg->id() | Message::IN_PROCESS_MSK);                      \
            timerMsg = msg;                                                    \
            timer.start(period);                                               \
            return Action(c);                                                  \
        }                                                                      \
                                                                               \
        Timer timer;                                                           \
        Message *timerMsg;                                                     \
                                                                               \
        bool early()                                                           \
        {                                                                      \
            return timer.early();                                              \
        }                                                                      \
                                                                               \
        _name();                                                               \
                                                                               \
    DISALLOW_COPY_AND_ASSIGN(_name)

/** End the definition of a StateFlow.
 */
#define STATE_FLOW_END() }

template <class T> class FlowInterface;

/** Runs incoming Messages through a State Flow.
 */
class StateFlowBase : public Executable
{
public:
    /** Callback from the executor. This function will be invoked when the
     * current stateflow gets the CPU. It will execute the current states until
     * the flow yields or is blocked in a waiting state. */
    virtual void run();

    /** Wakeup call arrived. Schedules *this on the executor. Does not know the
     * priority. */
    virtual void notify();

    /** Return a pointer to the service I am bound to.
     * @return pointer to service
     */
    Service *service()
    {
        return service_;
    }

protected:
    /** Constructor.
     * @param service Service that this state flow is part of
     * @param size number of queues in the list
     */
    StateFlowBase(Service *service)
        : service_(service)
        , state_(STATE(terminated))
    {
    }

    /** Destructor.
     */
    ~StateFlowBase()
    {
    }

    /* forward prototype */
    class Action;

    /** State Flow callback prototype
     */
    typedef Action (StateFlowBase::*Callback)();

    /** Return type for a state flow callback.
     */
    class Action
    {
    public:
        /** Constructor.
         */
        Action(Callback s) : nextState_(s)
        {
        }

        /** Get the next state for the StateFlowAction.
         */
        Callback next_state()
        {
            return nextState_;
        }

    private:
        /** next state in state flow */
        Callback nextState_;
    };

    /** Resets the flow to the specified state.
     * @param c is the state to continue the flow from after the next
     * notification.
     */
    void reset_flow(Callback c)
    {
        state_ = c;
    }

    /** Returns true if the state flow is in a specific state. */
    bool is_state(Callback c)
    {
        return state_ == c;
    }

    /** Resets the flow to the specified state and starts it.
     * @param c is the state to start the flow from.
     */
    void start_flow(Callback c)
    {
        HASSERT(state_ == STATE(terminated));
        yield_and_call(c);
    }

    /*========== ACTION COMMANDS ===============*/
    /* StateFlow implementations will have to use one of the following commands
     * to return from a state handler to indicate what action to take. */

    /** Call the current state again.
     * @return function pointer to current state handler
     */
    Action again()
    {
        return Action(state_);
    }

    /** Terminate current StateFlow activity.  The message instance is not
     * released before termination.  This is usefull if the message will be
     * reused for the purpose of sending to another StateFlow.
     * @return function pointer to terminated method
     */
    Action exit()
    {
        return STATE(terminated);
    }

    /** Terminates the flow and deletes *this. Do not access any member
     * function after this call has been made. */
    Action delete_this()
    {
        state_ = STATE(terminated);
        delete this;
        // Ensures that Run() does not touch the class member variables
        // anymore.
        return wait();
    }

#if 0
    /** Terminate current StateFlow activity. after releasing the message.
     * @param msg to release
     * @return function pointer to terminated method
     */
    templateAction release_and_exit(Message *msg)
    {
        msg->unref();
        return exit();
    }
#endif
    /** Imediately call the next state upon return.
     * @param c Callback "state" to move to
     * @return function pointer to be returned from state function
     */
    Action call_immediately(Callback c)
    {
        return Action(c);
    }

    /** Wait for an asynchronous call.
     * @return special function pointer to return from a state handler that
     * will cause the StateFlow to wait for an incoming wakeup (notification).
     */
    Action wait()
    {
        return Action(nullptr);
    }

    /** Wait for resource to become available before proceeding to next state.
     * @param c State to move to
     * @return function pointer to be returned from state function
     */
    Action wait_and_call(Callback c)
    {
        state_ = c;
        return wait();
    }

    /** Allocates a buffer from a pool and proceed to the next state when
     * allocation is successful.
     * @param target_flow defines the type of buffer to allocate.
     * @param c Callback "state" to move to after allocation
     * @param pool pool to allocate from; defaults to the pool of the target
     * flow.
     * @return function pointer to be returned from state function
     *
     * @TODO(balazs.racz) this hould be Pool* instead of DynamicPool*
     */
    template <class T>
    Action allocate_and_call(FlowInterface<Buffer<T>> *target_flow, Callback c,
                             Pool *pool = nullptr)
    {
        allocationResult_ = nullptr;
        Pool *p = pool;
        if (!p)
        {
            p = target_flow->pool();
        }
        LOG(VERBOSE, "allocate from pool %p, main pool %p", p, mainBufferPool);
        p->alloc_async<T>(this);
        return wait_and_call(c);
    }

    /** Allocates an entry from an asynchronous queue, and transitions to a
     * state once the allocation is complete.
     * @param c is the state to transition to after allocation
     * @param queue is the queue to allocate from.
     */
    Action allocate_and_call(Callback c, QAsync *queue)
    {
        allocationResult_ = nullptr;
        queue->next_async(this);
        return wait_and_call(c);
    }

    /** Takes the result of the asynchronous allocation without resetting the
     * object. This should be the first statement in the state where the
     * allocation transitioned. If you expect an empty object, use
     * \ref get_allocation_result() instead.
     * @param target_flow is the StateFlow for which we allocated.
     * @return The full buffer as it was inserted into the async queue. */
    template <class T>
    Buffer<T> *full_allocation_result(FlowInterface<Buffer<T>> *target_flow)
    {
        Buffer<T> *result = static_cast<Buffer<T> *>(allocationResult_);
        return result;
    }

    /** Takes the result of the asynchronous allocation. This should be the
     * first statement in the state where the allocation transitioned.
     * @param target_flow is the StateFlow for which we allocated.
     * @return an initialized buffer of the correct type. */
    template <class T>
    inline Buffer<T> *
    get_allocation_result(FlowInterface<Buffer<T>> *target_flow);

    /** Place the current flow to the back of the executor, and transition to a
     * new state after we get the CPU again.  Similar to @ref call_immediately,
     * except we place this flow on the back of the Executor queue.
     * @param c Callback "state" to move to
     * @return function pointer to be returned from state function
     */
    Action yield_and_call(Callback c)
    {
        state_ = c;
        notify();
        return wait();
    }

    /** Suspends execution of this control flow for a specified time. After
     * the timeout expires the flow will continue in state c.
     *
     * @param timer is the timer to start. This timer should be set up to
     * eventually call notify() on *this. We recommend using a StateFlowTimer.
     * @param timeout_nsec is the timeout with which to start the timer.
     * @param c is the next state to transition to when the timeout expires or
     * the timer gets triggered.
     */
    Action sleep_and_call(Timer *timer, long long timeout_nsec, Callback c)
    {
        timer->start(timeout_nsec);
        return wait_and_call(c);
    }

private:
    /** Service this StateFlow belongs to */
    Service *service_;

    /** Terminates the current StateFlow activity.  This is a sink state, and
     * there has to be an external call to do anything useful after this state
     * has been reached.
     * @returns delay.
     */
    Action terminated();

    /** Callback from a Pool in case of an asynchronous allocation. */
    virtual void alloc_result(QMember *b)
    {
        LOG(VERBOSE, "allocation result arrived.");
        allocationResult_ = b;
        notify();
    }

    /** current active state in the flow */
    Callback state_;

    /** The result of the next allocation that comes in. */
    QMember *allocationResult_;

    /** Default constructor.
     */
    StateFlowBase();

    DISALLOW_COPY_AND_ASSIGN(StateFlowBase);
};

template <class T, class S> class StateFlow;

/** A state flow that has an incoming message queue, pends on that queue, and
 * runs a flow for every message that comes in from that queue. */
class StateFlowWithQueue : public StateFlowBase, protected Lockable
{
public:
    /// Wakeup call arrived. Schedules *this on the executor.
    virtual void notify();

    /// @returns true if the flow is waiting for work.
    bool is_waiting()
    {
        AtomicHolder h(this);
        if (!queue_empty()) return false;
        return isWaiting_;
    }

protected:
    StateFlowWithQueue(Service *service)
        : StateFlowBase(service)
        , currentMessage_(nullptr)
        , currentPriority_(MAX_PRIORITY)
        , isWaiting_(1)
    {
        reset_flow(STATE(wait_for_message));
    }

    /** Entry into the StateFlow activity.  Pure virtual which must be defined
     * by derived class. Must eventually (through some number of states) call
     * release_and_exit() to transition to getting next message.
     * @return function pointer to next state
     */
    virtual Action entry() = 0;

    /** Takes the front entry in the queue. Must be called with the lock held.
     *
     * @returns NULL if the queue is empty.
     * @param priority will be set to the priority of the queue member removed
     fomr the queue. */
    virtual QMember *queue_next(unsigned *priority) = 0;

    /** Returns true if there are no messages queued up for later
     * processing. */
    virtual bool queue_empty() = 0;

    /// @returns the current message we are processing.
    Message *message()
    {
        return currentMessage_;
    }

    /** Sets the current message being processed. */
    void reset_message(Message* message, unsigned priority) {
        HASSERT(!currentMessage_);
        currentMessage_ = message;
        currentPriority_ = priority;
    }

    /// @returns the priority of the message currently being processed.
    unsigned priority()
    {
        return currentPriority_;
    }

private:
    STATE_FLOW_STATE(wait_for_message);

    /// Message we are currently processing.
    Message *currentMessage_;

    /// Priority of the current message we are processing.
    unsigned currentPriority_ : 31;

    /** True if we are in the pending state, waiting for an entry to show up in
     * the queue. Protected by Lockable *this. */
    unsigned isWaiting_ : 1;

    template <class T, class S> friend class StateFlow;
    friend class GlobalEventFlow;

    static const unsigned MAX_PRIORITY = 0x7FFFFFFFU;
};

template <class MessageType> class FlowInterface
{
public:
    /** @returns the buffer pool to use for sending messages to this flow. This
     * is to be used as a hint, the caller is allowed to send buffers from
     * different source.
     * @todo(stbaker) change this to Pool* once it supports async alloc. */
    virtual DynamicPool *pool()
    {
        return mainBufferPool;
    }
    virtual void send(MessageType *message, unsigned priority = UINT_MAX) = 0;

    /** Synchronously allocates a message buffer from the pool of this flow. */
    MessageType *alloc()
    {
        MessageType *ret;
        pool()->alloc(&ret);
        return ret;
    }

    /** Asynchronously allocates a message buffer from the pool of this
     * flow. Will call target->AllocationCallback with the pointer. The callee
     * shall come back and use cast_alloc to turn the pointer into a usable
     * object. */
    void alloc_async(Executable *target)
    {
        typedef typename MessageType::value_type T;
        Pool* p = pool();
        p->alloc_async<T>(target);
    }

    /** Down casts and initializes an asynchronous allocation result to the
     * appropriate flow's buffer type.
     *
     * @param entry is the value that got returned by allocation_callback of
     * this pool.
     * @returns a default-constructed (zeroed) message for this flow. */
    static MessageType *cast_alloc(QMember *entry)
    {
        MessageType *result;
        Pool::alloc_async_init(static_cast<BufferBase *>(entry), &result);
        return result;
    }
};

template <class T>
Buffer<T> *
StateFlowBase::get_allocation_result(FlowInterface<Buffer<T>> *target_flow)
{
    return target_flow->cast_alloc(allocationResult_);
}

template <class MessageType, class QueueType>
class StateFlow : public StateFlowWithQueue, public FlowInterface<MessageType>
{
public:
    /** Constructor.
     * @param service Service that this state flow is part of
     * @param size number of queues in the list
     */
    StateFlow(Service *service) : StateFlowWithQueue(service)
    {
    }

    /** Destructor.
     */
    ~StateFlow()
    {
    }

    /** Sends a message to the state flow for processing. This function never
     * blocks.
     *
     * @param msg Message to enqueue
     * @param priority the priority at which to enqueue this message.
     */
    void send(MessageType *msg, unsigned priority = UINT_MAX)
    {
        AtomicHolder h(this);
        queue_.insert(msg, priority);
        if (isWaiting_)
        {
            isWaiting_ = 0;
            currentPriority_ = priority;
            this->notify();
        }
    }

protected:
    using StateFlowBase::call_immediately;
    using StateFlowBase::Callback;

    /** Takes the front entry in the queue.
     *
     * @returns NULL if the queue is empty.
     * @param priority will be set to the priority of the queue member removed
     fomr the queue. */
    virtual QMember *queue_next(unsigned *priority)
    {
        typename QueueType::Result r = queue_.next();
        if (r.item)
        {
            *priority = r.index;
        }
        return r.item;
    }

    bool queue_empty() {
        AtomicHolder h(this);
        return queue_.empty();
    }

    /** @returns the current message we are processing. */
    MessageType *message()
    {
        return static_cast<MessageType *>(StateFlowWithQueue::message());
    }

    /** Releases the current message buffer back to the pool it came from. The
     * state flow will continue running (and not get another message) until it
     * reaches the state exit(). */
    void release()
    {
        if (message())
        {
            message()->unref();
        }
        currentMessage_ = nullptr;
    }

    /** Terminates the processing of this flow. Takes the next message and
     * start processing agian from entry().*/
    Action exit()
    {
        return call_immediately(STATE(wait_for_message));
    }

    /** Terminates the processing of the current message. Flows should end with
     * this action. Frees the current message.
     * @return the action for checking for new messages.
     */
    Action release_and_exit()
    {
        release();
        return exit();
    }

    /** Releases ownership of the current message.
     * @return the current message.
     */
    MessageType *transfer_message()
    {
        MessageType *m = message();
        currentMessage_ = nullptr;
        return m;
    }

    /** Entry into the StateFlow activity.  Pure virtual which must be
     * defined by derived class.
     * @return function pointer to next state
     */
    virtual Action entry() = 0;

private:
    /** Implementation of the queue. */
    QueueType queue_;
};

/** Use this timer class to deliver the timeout notification to a stateflow.
 *
 * Usage:
 *
 * in the StateFlow class create a variable
 *   StateFlowTimer timer_;
 * in the constructor initialize it with
 *   , timer_(this).
 * then in the state function do
 *   return sleep_and_call(&timer_, MSEC_TO_NSEC(200),
 *STATE(next_after_timeout);
 * If needed, you can wake up the timer in a handler function by calling
 * timer_.trigger(). This will transition to the new state immediately.
 */
class StateFlowTimer : public Timer
{
public:
    StateFlowTimer(StateFlowBase *parent)
        : Timer(parent->service()->executor()->active_timers())
        , parent_(parent)
    {
    }

    virtual long long timeout()
    {
        parent_->notify();
        return NONE;
    }

protected:
    /// The timer will deliver notifications to this flow.
    StateFlowBase *parent_;
};

#endif /* _StateFlow_hxx_ */
