# Output command policy

`OutputController::deriveCommand()` is a pure, host-testable transformation
from one `RiskAssessment` to an abstract command. It performs no GPIO, driver,
timing, or other hardware operation.

## Risk-state mapping

| `RiskState` | `OutputCommandKind` |
|---|---|
| `Startup` | `Startup` |
| `Normal` / `Safe` alias | `Normal` |
| `Monitor` | `Monitor` |
| `DangerTest` / `HighRiskTest` alias | `DangerTest` |
| `Unknown` | `Unknown` |

These are risk-state labels only. They do not define a buzzer, relay, LED,
electrical level, pin, duration, priority, or physical safe state. In
particular, `Normal` is not an instruction to switch an output off, and
`Unknown` is never converted to `Normal`. A separate approved, versioned
alarm policy and reviewed hardware contract are required before any consumer
may actuate hardware.

An assessment with `valid == false`, an invalid `RiskState`, or an invalid
`AlarmOutputState` produces `OutputCommandKind::None` with an explicit
non-valid status. No fallback command is issued.

## Feedback and repeat behavior

The result's `feedback` field preserves the assessment's recognized
`AlarmOutputState`. `NotPresent` and `NotVerifiable` are valid feedback
states; they do not prevent deriving the abstract risk command. The controller
does not infer or claim that a command was applied, accepted, or physically
verified. An invalid feedback enum is reported as `InvalidFeedbackState`, with
the result feedback set to `NotVerifiable`.

The function is stateless: identical input assessments produce identical
commands. It does not implement debounce, hysteresis, output sequencing,
duplicate suppression, or transition timing.

## Validation

The native unit tests cover each declared `RiskState` (including aliases),
unknown and invalid values, invalid assessments, repeated inputs, and
`NotPresent` / `NotVerifiable` feedback.
