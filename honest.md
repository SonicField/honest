HONEST
An antifragile declarative interchange format
Goal Document · v0.1 Draft · NBS Framework


Honest is a Pascal-inspired interchange format designed to be read without tooling by humans and AI alike, parsed efficiently by machines, and to remain coherent in the face of schema evolution, version skew, and incomplete knowledge. It is the wire format of the NBS multi-agent framework and an explicit rejection of JSON's structural poverty and CORBA's contractual brittleness.


1. Problem Statement
JSON is ubiquitous because it arrived first, not because it is good. It encodes shape but not meaning. It has no discriminated unions, no semantic primitive types, no epistemic metadata, and no concept of a schema contract. Consumers must reconstruct intent from field names and out-of-band documentation written in natural language. For human-to-machine communication this is tolerable. For machine-to-machine communication at the semantic level required by AI agents, it is structurally inadequate.
The alternative tradition — IDL, CORBA, Protobuf — understood that a wire format should carry type contracts. It failed by confusing rigour with rigidity. A total contract that cannot survive deviation is not robust; it is fragile dressed as precision. Any mismatch between sender and receiver is catastrophic rather than recoverable.
AI agent interchange requires a third path: a format rich enough to carry meaning, honest enough to carry its own schema, and robust enough to survive the conditions of the real world — version skew, partial knowledge, and consumers that were not anticipated at design time.

2. Design Goals
2.1 Three Equal Consumers
Honest must serve three distinct consumers without privileging any of them.
A human must be able to read an Honest document and understand its meaning without a parser, schema browser, or documentation.
An AI agent must be able to interpret an Honest document cold — using the declarations in the document itself as the schema, with no external context required.
A machine parser must be able to deserialise an Honest document efficiently, using a rulebook to accelerate but not depend upon it.
This trinity is not incidental. It reflects the operational reality of the NBS framework, where humans author, agents communicate, and programmatic systems route and store. A format that serves all three equally is intrinsically more durable than one optimised for any single consumer.
2.2 Antifragility
Honest must become more capable, not less, as it encounters variation. This is the inversion of both JSON (which is indifferent to variation) and CORBA (which is destroyed by it).

Unknown declarations are never errors. They are always information. A receiver encountering a declaration it has not seen before comes out the other side with an extended rulebook, not a failed parse.


Schema mismatch is a signal, not a fault.
Unknown fields in the declarations section are preserved and passed through.
A receiver without any prior rulebook can reconstruct meaning from the declarations embedded in the message itself.
A receiver with a full rulebook parses faster. The rulebook accelerates; it does not define.

2.3 Declarations Travel With Data
Every Honest message is self-describing. The declarations section precedes the data section in every transmitted artifact. This is the mechanism by which antifragility is achieved: the schema is not held out-of-band and assumed shared. It is in the wire.
This architecture was proven in production in a tier-one banking system. The deserialiser compares incoming declarations against its rulebook to accelerate parsing. Where they agree, it goes fast. Where they diverge, the wire format is ground truth. The system has never required a version lockstep.
2.4 Pascal as Foundation
The syntax and type system of Honest are grounded in Pascal. This is a deliberate choice, not a nostalgic one. Pascal has properties that make it uniquely suitable as the basis for a meaning-carrying interchange format:

Its keywords are full English words. The syntax reads as a declaration, not an instruction.
The variant record provides discriminated unions natively. The tag is part of the type, not a convention.
Enumerated types are first-class. Meaning is encoded in the type system, not in string values.
It was designed to be readable by an intelligent but uninitiated reader. That design goal transfers directly to AI consumers.
It has a totemic, documentary quality. Reading a Pascal type declaration is reading a contract.

Wirth designed Pascal to teach. A format designed to be explained to students is also a format that can be explained to a language model. The pedagogical quality is load-bearing, not incidental.

3. Format Specification Goals
3.1 Document Structure
An Honest document has two mandatory sections, in order:

DECLARATIONS — all type definitions required to interpret the data section.
DATA — one or more typed variable assignments conforming to the declared types.

Both sections must be present in every transmitted artifact. A document without declarations is not Honest.
3.2 Illustrative Example
The following is illustrative of the target syntax, not a binding specification.
pascal{ DECLARATIONS }

type
  Confidence  = (Certain, High, Moderate, Low, Speculative);
  Provenance  = (Observed, Inferred, Recalled, Generated);
  AgentRole   = (Supervisor, Gatekeeper, Scribe, Theologian,
                  Generalist, Testkeeper);
  ClaimKind   = (Assertion, Query, Correction, Partial, Error);

  Claim = record
    confidence : Confidence;
    provenance : Provenance;
    as_of      : LongInt;
    case kind  : ClaimKind of
      Assertion  : (content  : ShortString);
      Query      : (intent   : ShortString; scope : ShortString);
      Correction : (replaces : LongInt; content : ShortString);
      Error      : (code     : Word; reason : ShortString);
  end;

  Message = record
    id      : LongInt;
    sender  : AgentRole;
    payload : Claim;
  end;

{ DATA }

var message : Message = (
  id      : 4721;
  sender  : Theologian;
  payload : (
    confidence : High;
    provenance : Inferred;
    kind       : Assertion;
    content    : 'The compilation target is ARM Thumb2';
  )
);
An AI reading this document cold has everything it needs. The enumerated types are self-explanatory. The variant record structure tells the reader which fields are present and why. No external documentation is required to reconstruct intent.
3.3 Type System Requirements
The Honest type system must support, at minimum:

Enumerated types with named values.
Records with named, typed fields.
Discriminated unions via variant records. The discriminant tag is mandatory and typed.
Semantic primitive tags distinguishing natural language strings from identifiers, URIs, and other string subtypes.
Fixed-width numeric types with explicit semantics (Word, LongInt, Real, etc.).
Epistemic metadata types: Confidence and Provenance as first-class enumerated types, not annotations.
Temporal scope: an as_of field convention for contextual validity.

3.4 Schema Posture
Honest schemas are heuristic, not prescriptive. They assist deserialisation; they do not define what is permitted to exist. This distinction is fundamental to the antifragile design.

The receiver's rulebook is a performance optimisation.
The declarations in the wire format are the ground truth.
Where rulebook and declarations agree, the parser accelerates.
Where they diverge, the parser reasons from declarations and extends its rulebook.
Divergence is logged as information, never raised as an error.


4. Design Principles
PrincipleMeaningAnti-patternSelf-describingEvery message carries its own schema. No external contract is required to interpret it.Out-of-band schema registriesAntifragileUnknown declarations extend the receiver's knowledge. Deviation is signal, not fault.Version lockstep, strict validationMeaning-carryingThe type system encodes intent. Field names are not the primary semantic carrier.Stringly-typed JSON conventionsTotemic syntaxThe format reads as a declaration to any intelligent reader, human or AI, without tooling.Opaque binary, terse abbreviationsHonest provenanceEvery claim carries its epistemic status as a typed value, not an optional annotation.Confidence buried in prose fieldsTwo-face unityDocument and wire representations are the same artifact. The wire format is derived, never separately authored.Schema-first with generated models

5. Non-Goals
The following are explicitly out of scope for Honest:

Binary wire efficiency as a primary concern. Honest is human and AI readable first. Binary derivations may be specified later.
Backward compatibility with JSON, XML, or any existing format. Honest is not an evolution of those formats.
A schema registry or centralised type authority. Types travel with the message.
Runtime schema enforcement as a gatekeeping mechanism. Honest parsers do not reject; they reason.
General-purpose application serialisation. Honest is optimised for agent communication and semantic interchange, not object persistence.


6. Relationship to NBS
Honest is the native interchange format of the NBS multi-agent framework. NBS agents communicate exclusively in Honest. The format is designed to carry the specific semantic requirements of NBS agent messages:

Agent roles (Supervisor, Gatekeeper, Scribe, Theologian, Generalist, Testkeeper, Pythia, Shepherd) are first-class enumerated types.
The claim structure maps directly to NBS speech acts: assertion, query, correction, partial, error.
Epistemic metadata (Confidence, Provenance) reflects the Popperian falsification principles on which NBS operates.
The antifragile schema posture reflects NBS's tolerance for emergent agent behaviour — messages may carry structure that was not anticipated at framework design time.

Honest is fully open source under the same terms as NBS. Its applicability extends beyond NBS to any system requiring semantically rich, human-readable, AI-interpretable interchange.

7. Success Criteria
The Honest format specification is complete when:

A developer can implement a conformant Honest parser from the specification alone, with no reference implementation required.
An AI agent presented with an Honest message it has never seen before can interpret it correctly using only the declarations in the message.
A human developer can read an Honest message and describe its meaning accurately without documentation.
A receiver encountering a declaration unknown to its rulebook does not fail, and correctly logs the new type for future acceleration.
The serialised wire representation of a document-format Honest file is mechanically derivable, with no information loss.
The NBS agent suite communicates exclusively in Honest with no regression in capability.


8. Open Questions
The following are deferred to the specification phase:

String subtype syntax: should semantic tags appear as type qualifiers (string:uri) or as distinct named types (Uri = string)?
Temporal scope: is as_of a convention or a mandatory field in the Claim record?
Nesting depth: what is the practical limit on recursive record nesting for wire efficiency?
Comments and documentation: should Honest support inline documentation as a first-class syntax element, distinct from code comments?
Binary derivation: should the specification define a canonical binary representation, or leave this to implementors?
Rulebook format: how is the receiver rulebook itself represented and versioned?


Honest is a component of the NBS (No Bull Shit) framework. This document is a goal statement, not a formal specification. Implementation decisions should be validated against the three-consumer test: would this be readable by a human, interpretable by an AI, and parseable by a machine?

See ~/claude_docs/nbs-framework
