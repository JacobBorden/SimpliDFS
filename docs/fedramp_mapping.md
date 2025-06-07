# FedRAMP RMF Control Mapping

This document begins the mapping of SimpliDFS security controls to the NIST SP 800-53 Rev. 5 controls required for FedRAMP compliance. The table outlines implemented features in the project and identifies the corresponding RMF control family. Additional controls will be documented as development progresses.

| RMF Control | Implementation Detail | Status |
|-------------|----------------------|--------|
| **AC-2**    | Node registration with YAML-defined RBAC policy enforcement | Implemented |
| **AU-2**    | Audit log chain verified daily by `AuditVerifier` | Implemented |
| **SC-8**    | TLS encryption for all client-server communication | Implemented |
| **SC-28**   | Replica integrity validated through `ReplicaVerifier` | Implemented |
| **SI-7**    | Fuzz testing and chaos tests for resilience | Implemented |

This mapping is preliminary and does not constitute a full System Security Plan (SSP).

To prepare artifacts for an Authorization to Operate submission, run
`scripts/generate_ato_bundle.sh`. The script collects audit logs, FIPS
self-test results and the `rbac_policy.yaml` file, packaging them into
`bundle/ato_<date>.tar.gz`.
