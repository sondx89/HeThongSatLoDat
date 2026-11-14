# Optional buzzer alarm output

**Status:** Host-tested state machine; GPIO, polarity and reset-safe electrical
behavior remain unapproved.

`AlarmService` consumes a copied `RiskAssessment` and `OutputController`
command; it never changes GPIO from `RiskManager`. SAFE requests OFF, WATCH
repeats three 200 ms ON pulses with 200 ms OFF intervals in a 1200 ms cycle,
DANGER requests continuous ON, and invalid/unknown/startup input requests OFF.
The timing state machine is cooperative and performs no delay.

`BuzzerDriver` is disabled by default. Enabling it requires a configured pin
and explicitly verified active-high/active-low polarity. The pin adapter must
implement finite, task-context operations. Command state and hardware feedback
are separate: absent feedback is `NotVerifiable`; an absent buzzer is
`NotPresent`; write/read mismatch is `Failed`. A successful GPIO write is not
treated as proof that sound was emitted.

The DOCX GPIO11/active-low reference has not been approved and is not installed.
The current board pin map and output electrical review are still TBD. Software
sets the inactive logical output during `begin()`; safe reset behavior before
software initialization must be established electrically by the approved BSP
and board design.

The alarm service is not yet registered with `TaskFramework` or connected to a
production GPIO adapter. Until an approved composition explicitly starts its
single owning task, the buzzer remains disabled; the host-tested state machine
does not imply a physical alarm is active.

No buzzer or feedback hardware was available, flashed or physically tested.
