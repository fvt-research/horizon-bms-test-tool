# BMS integration suite plan

Status: draft for discussion. The architecture and delivery sequence below are
proposals, not evidence that the hardware or existing tests are commissioned.

## Summary

Incrementally extend the existing C++ BMS test tool into a single desktop
application to display BMS status, manually control the HIL, run automated Lua
test scripts, and record testing telemetry. Test fixtures and small fakes will
support focused checks, while the real HIL will validate physical behavior.
A shared HIL runtime will own the devices and expose an actions-and-reactions
interface to the GUI and scripts. Tests will establish and verify initial
conditions, apply physical stimuli, observe BMS responses through CAN and RTT
where supported, and record results and cleanup outcomes. The simulator's
Lua/sol2 scenario system provides useful inspiration, but physical HIL behavior
and timing must be verified independently. The final UI, hardware mappings,
observation contracts, and many implementation details remain open.

## Goals and boundaries

- Show live BMS status, measurements, faults, and relevant HIL device state.
- Support manual HIL control and queued automated tests in the same GUI.
- Accept messages requesting named, approved tests and return progress and
  results; local GUI requests use the same test execution path.
- Keep BMS and HIL status visible while a test is running.
- Record CAN, RTT, test events, and HIL actions with each run.
- Define and verify initial conditions so each case starts from a known state.
- Keep the runtime and GUI in C++, with Lua as the test scripting language;
  no Python runtime should be required for the finished application.
- Deliver small, usable increments on the existing tool, using focused fixtures
  and real HIL checks as each relevant physical capability becomes available.

The application orchestrates tests against the board and physical HIL; it does
not make the board or bench deterministic. Timeouts bound how long the runner
waits, rather than promising deterministic device behavior. Software controls
do not replace independent electrical protection or approved bench procedures.

## Proposed architecture

```text
GUI controls and test requests     External test requests/results
              |                              |
              +-------- Test request --------+
                              |
                      C++ HIL runtime
           +------------------+-------------------+
      Lua test runner    Observation/recorder    Device drivers
                                                    |
                                  BMS CAN, Chroma, ADAM, relay, RTT
```

- **HIL runtime:** one owner of device connections, command scheduling,
  configuration, observation state, run lifecycle, and cleanup. GUI controls
  and Lua scripts use the same validated operations.
- **Devices:** C++ adapters for BMS CAN, Chroma CAN, ADAM serial and the
  Modbus relay, plus an RTT observation adapter. Confirm actual bench topology
  and capabilities before selecting transports and mappings.
- **Observations:** timestamped values with source, validity and freshness.
  Keep commanded setpoints distinct from measured or reported values.
- **Runner:** discover approved scripts, queue selected tests, execute one at a
  time, publish progress, and handle cancellation and failures without
  blocking the GUI. A test cannot bypass runtime ownership or safety checks.
- **Recorder:** capture data independently of GUI rendering. Persist raw CAN
  RX and TX, RTT output, HIL commands, test events, results, configuration,
  firmware and DBC identity, and cleanup outcomes. Report dropped data and
  recording failures explicitly.
- **GUI:** retain useful existing status, fault, plot and parameter views;
  add manual HIL control, test selection and queue, progress, stop, results,
  device health and recording status.

Move periodic BMS command transmission out of the existing GUI panel's render
path. It must continue independently of the selected panel, under runtime
ownership. While a test owns control, manual changes must be rejected or
explicitly arbitrated; observation remains available.

## Test fixtures and physical HIL

Start with fixtures that establish and clean up each test's known conditions.
Use small fakes for focused component tests and for failures that are difficult
or unsafe to reproduce on the bench, such as malformed device replies and
cleanup failures. Do not build a complete mock HIL upfront.

The existing virtual CAN interface is a loopback, not a BMS or a HIL. The
separate BMS simulator executes application logic and may complement focused
tests, but neither a fake nor a simulator pass proves physical wiring,
firmware behavior or bench safety. If interactive hardware-free testing becomes
necessary later, consider exposing suitable fakes through a clearly labeled
application mode.

## Message-based test requests

The initial message interface requests a named, approved test and a validated
profile; it does not transmit executable Lua. A request identifies the caller's
request ID, test and profile. The application validates the request and its
control permissions, then either rejects it with a reason or accepts it and
returns a run ID. Acceptance is not a test result.

The runner reports queued, running and terminal outcomes, including DUT
assertion failures, infrastructure errors, cancellation and cleanup status.
Requests, run IDs and results are recorded with the test evidence. Duplicate
request handling must prevent accidental duplicate execution. The GUI and any
external message adapter submit through the same validated queue; neither gets
direct ownership of the equipment.

Gigadoot's existing messaging is a candidate transport, but the test-request
message schema, transport, versioning and external client interface still need
design and implementation. Before enabling non-local access, define and
enforce authentication, authorization and transport protection. Receiving
arbitrary Lua or sequences of unapproved actions is outside the initial
interface.

## Lua tests: initial state, action, reaction

Use Lua directly with sol2 as the C++ binding, following the simulator's
approach. Gigadoot is not a scripting dependency; its messaging remains a
possible future transport for approved test requests.

The local `LuaScriptSession` prototype executes log/send actions and manages
delayed actions, pause, stop and cancellation. A fake adapter exercises the
lifecycle without HIL hardware. It is not connected to the GUI or physical
devices. Scripts must be trusted: the prototype is not sandboxed and does not
enforce execution time limits.

Adapt message-oriented actions and observations to BMS CAN, RTT and HIL
devices, and add initial-state checks, expected reactions, results and cleanup.
The HIL runtime remains the sole owner of physical devices. Those HIL features
and a complete test runner are not yet present.

Each test should express:

1. **Initial state:** apply a named, reviewed bench profile and drive the BMS
   through supported commands to the required state. Verify prerequisites,
   fresh observations and device health before applying the test stimulus.
2. **Action:** apply an engineering-unit stimulus or named fault injection
   through a validated runtime operation.
3. **Reaction:** wait for fresh, source-qualified observations and check the
   expected state, fault, outputs or measurements within an agreed window.
4. **Recovery:** remove the stimulus and verify the specified clearing, latch
   or recovery behavior.
5. **Cleanup:** perform the approved cleanup even after failure or cancellation;
   record failures and unknown device states.

The runner, rather than an individual script, owns cleanup and records whether
it succeeded. Distinguish DUT assertion failures from setup, communication,
observation, recording, cancellation and cleanup failures. Missing or stale
evidence must not produce a pass.

The simulator currently schedules actions on simulated ticks and can inspect
internal fault state directly. HIL must instead use physical time and
available observations. Share scenario concepts or scripts only where
capabilities and semantics actually match: for example, a fixed relay circuit
does not necessarily implement an arbitrary isolation-resistance setpoint.
Reject unsupported actions explicitly rather than silently approximating them.

## CAN and RTT

- **CAN:** use for BMS commands, published status and measurements, and
  parameter access where the installed firmware supports it. Reconcile the test
  suite's legacy DBC and commands with the test tool's DBC and the firmware
  under test; do not assume the definitions are interchangeable.
- **RTT:** provide a separate observation path for logs and, where implemented,
  structured internal status. Verify initialization, available fields, loss
  behavior and target impact before relying on it. A required internal-state
  assertion must explicitly name a verified observation source.
- **Time correlation:** preserve source timestamps where available and record
  host receive times. Document clock domains and synchronization uncertainty;
  do not assume CAN, RTT and host clocks are aligned.

The existing GUI has a CANopen parameter panel; its supported operations and
compatibility with the installed BMS firmware must be checked before using it
to establish test conditions.

## Incremental delivery

Each increment builds on the existing tool and ends with a usable outcome and
focused checks. Use fixtures and small fakes where useful; validate actual
physical behavior on the HIL as it becomes available. Inventory the firmware,
DBC, wiring, calibration, limits, interlocks and approved procedures before
commissioning any physical control.

1. **Stabilize the existing tool.** Run cyclic BMS commands independently of
   the selected panel, stop them on disconnect and report transmit failures.
   **Status:** Implemented, with a focused fake-transport unit test. The
   combined changes and PR #1 build and launch on Windows; the panel-switch
   comparison and physical CAN behavior remain to be verified. PR #1 is not
   included in the implementation commit.
2. **Add Lua action infrastructure.** Use Lua and sol2 in a GUI-independent
   session in this repository, with delayed sends, pause, stop and cancellation.
   Demonstrate start, observation-triggered action, stop and cancellation with
   a small fake adapter.
   **Status:** Implemented as a local prototype; fake-adapter tests pass on
   Linux. Windows validation, GUI and HIL adapters remain.
3. **Add one manual HIL control.** Connect one reviewed device operation to
   the application runtime and GUI, with its reported state and errors.
   Exercise invalid requests and communication failures using fixtures, then
   commission it on the attended bench. Select the device and operation after
   confirming the installed wiring.
4. **Run one named Lua test locally.** Use the Lua action infrastructure
   and the same runtime to prepare and verify an initial state, perform an
   action, check a reaction and clean up. Submit the approved test by name
   through a common request path. Check success, failed prerequisites,
   missing reactions, cancellation and cleanup failure.
5. **Add the GUI queue and run evidence.** Select approved tests, queue them
   sequentially and inspect progress while switching panels. Record per-run
   actions, observations, results and available CAN/RTT traffic. Verify manual
   control arbitration, cancellation, duplicate requests and recording errors.
6. **Accept external test-request messages.** Add a message adapter that
   submits named approved tests to the same queue and returns acceptance,
   progress and terminal results. Evaluate Gigadoot as a transport at that time.
   Validate request IDs, profiles and permissions; protect non-local access.
   Do not accept arbitrary remote Lua. The transport and schema remain to be
   agreed.
7. **Commission and expand physical coverage.** Complete one reviewed
   cell-voltage fault scenario on the board and HIL, then add HVIL, isolation,
   contactor-feedback and other supported cases. Port only needed adapters,
   validate protocol failures with fixtures and commission each physical path.
   Verify recovery and recording, then package and document the application.
   Validate Modbus replies before relay commissioning and serialize shared
   serial transactions if the installed topology requires it.

Track any required firmware-side observability or parameter changes separately
in the BMS application worktree. A fake may exercise a proposed interface
before firmware changes exist, but the capability is not physically supported
until verified against the actual board.

## Safety and verification requirements

- Validate configurations, channel identities, units, limits and requested
  actions before writing to hardware. A command acknowledgement alone is not
  proof of the physical state.
- Use bounded device operations and interruptible waits; supervise script
  execution. A Lua instruction hook cannot interrupt a blocked native call,
  so adapters also need timeouts and error propagation.
- Attempt independent cleanup actions even when one fails, and show incomplete
  cleanup to the operator. Do not automatically resume or energize an
  interrupted run after restart.
- Use fakes for focused checks where useful. Physical tests require approved
  procedures, verified wiring and independent protective measures.
- Reconcile thresholds, detection and clearing timing, fault actions and
  observability with the specific DUT firmware and parameters. Existing tests
  and historical logs are references, not proof of present coverage.

## Decisions still to make

- Final UI layout, repository ownership, packaging and supported platforms.
- Lua version, script API, test discovery and metadata.
- Initial-state profiles, relay-to-fault mappings, physical readback and
  acceptable cleanup behavior.
- Firmware/DBC compatibility, supported CAN parameter operations and RTT
  fields, formats, availability and impact.
- Required logging format, retention, timestamp correlation and behavior
  when recording is incomplete.
- Test-request message schema, transport, external access policy and clients.
- Which tests and requirements take priority after the first vertical slice,
  and whether external model or database integrations are needed later.

## Reference material

- [HIL suite engineering handoff](https://github.com/fvt-research/horizon-bms-test-suite/blob/29a16e6cdc9a5b7c4e644062c64e73c58ad476db/docs/handoff.md):
  current Python devices, tests and known limitations.
- [BMS test tool architecture](https://github.com/fvt-research/horizon-bms-test-tool/blob/6b3c26507e21f5390ba64bbc951408a550db848d/docs/architecture.md):
  existing C++ GUI and CAN model.
- [Gigadoot Lua dispatcher](https://github.com/fvt-research/gigadoot/blob/487d7c67e7235ad58a22362507538341bfe631d7/giga_core/docs/components/lua_script_dispatcher.md):
  lifecycle/message dispatch and actions returned to the runtime.
- Local simulator reference: `../stafl_bms_app_bms-sim/sim/`, especially
  `sim_platform/inc/sim_action.h`, `sim_platform/inc/scenario.h`,
  `src/lua_scenario.cpp` and `scenarios/`. Its checkout revision could not be
  verified in the current environment.
