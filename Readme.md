# Efficient Connection Management System with Circuit Breaker Mechanism

This repository implements an efficient system that integrates connection management with a circuit breaker mechanism. The system includes connection abstract classes and derived classes, utilizing the connection factory pattern to create different types of connections. It effectively manages the acquisition, release, and cleanup of connections through a connection pool.

## Features

- **Connection Pool**: Supports multithreaded access, providing a flexible timeout handling mechanism. It periodically checks and cleans up expired connections to ensure efficient resource utilization.
  
- **Circuit Breaker HTTP Client**: Developed an HTTP client with a circuit breaker pattern. When the service is unstable, the client can automatically enter circuit breaker mode and start a recovery thread to monitor the service's health status. This mechanism ensures that multiple instances share a single recovery thread through atomic flags, effectively preventing resource waste.

## Background

Previously, the system created a new connection for each request. While this approach simplified the implementation and avoided issues related to connection state pollution, it led to frequent creation and destruction of connections, increasing system overhead and degrading performance.

By utilizing a connection pool, we can reduce the overhead of connection creation and destruction, thereby improving system performance.

### Connection Management

The key aspects of connection management include:
1. **Idle Timeout**: Each connection should have an idle timeout based on the last usage time. The system compares the current time with the last usage time to determine if the connection has timed out.
2. **Connection Establishment and Termination**: Connections can be modeled as an abstract class, with derived classes overriding the connection establishment and termination functionalities as needed.
3. **Connection Factory**: A connection factory creates connections. Different derived connection types can have their own connection factories, allowing for a flexible design.

### Connection Pool Functionality

The connection pool implements the following functionalities:

1. **Acquire Connection**:
   - Search for a non-expired connection in the idle pool and move it to the busy pool.
   - Implement thread synchronization to determine whether to wait for a usable connection based on the provided `timeout_time`:
     - `timeout_time == -1`: Block indefinitely until a connection becomes available.
     - `timeout_time == 0`: Do not wait; return immediately.
     - `timeout_time > 0`: Wait for the specified time or until a connection becomes available.
   - If no suitable connection is found and the current destination has not reached the maximum connection limit, create a new connection using the connection factory, set the idle timeout, and move it to the busy pool.

2. **Release Connection**:
   - Move the connection from the busy pool to the idle pool and update the last usage time for future idle timeout calculations.

3. **Cleanup Connections**:
   - Periodically check the idle pool for expired connections and remove them. If all connections in the pool are deleted, the pool object itself will also be deleted.

### Fuse HTTP Client

The Fuse HTTP Client implements the circuit breaker pattern with the following functionalities:

![fuse client](images/fuseclient.png)

1. **Send Requests**:
   - Before sending a request, check if the client is in circuit breaker mode and whether it is the recovery thread.
   - The recovery thread can still send requests to check the service health, even when in circuit breaker mode.

2. **Circuit Breaker Logic**:
   - If the client is in circuit breaker mode and not the recovery thread, any incoming requests during the recovery process will be cleared.
   - If the recovery thread successfully restores service, reset the circuit breaker count to zero and exit circuit breaker mode.

3. **Request Handling**:
   - Acquire a connection from the pool for the current destination. If it is the recovery thread, retry requests according to the specified retry count.
   - If not the recovery thread, send a request directly to the destination.
   - Release the connection back to the pool and evaluate the response:
     - If the response is a 5xx error or exceeds the maximum delay, increment the circuit breaker count. If the count exceeds the threshold, enter circuit breaker mode and start the recovery thread.
     - Otherwise, return the result directly.

4. **Recovery Process**:
   - When the recovery thread is activated, it will periodically request the service's health endpoint. If successful requests exceed a defined threshold, reset the circuit breaker count and mark the circuit breaker mode as false, indicating recovery completion.

### Multithreading Considerations

When derived classes of the Fuse HTTP Client are used, each client has its own private circuit breaker flag, but the recovery thread's flag is public (atomic). This ensures that when a client enters circuit breaker mode, only one recovery thread is activated. Once the recovery thread completes, all clients that entered circuit breaker mode will also recover.

## Conclusion

This repository provides a robust system for managing connections and enhancing stability through a circuit breaker mechanism. It effectively reduces overhead, improves performance, and ensures efficient resource utilization while maintaining system resilience.
