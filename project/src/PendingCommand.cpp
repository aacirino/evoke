#include "PendingCommand.h"
#include "File.h"

PendingCommand::PendingCommand(const std::string& command) 
: commandToRun(command)
{
}

void PendingCommand::AddInput(File* input) {
  inputs.push_back(input);
  input->listeners.push_back(this);
  Check();
}

void PendingCommand::AddOutput(File* output) {
  if (output->generator) 
    throw std::runtime_error("Already found a command to create this output file");

  output->generator = this;
  output->state = File::Unknown;
  outputs.push_back(output);
  Check();
}

void PendingCommand::Check() {
  if (outputs.empty()) return;
  if (state == PendingCommand::ToBeRun) return;
  bool missingOutput = false;
  std::time_t oldestOutput = outputs[0]->lastwrite();
  for (auto& out : outputs) {
    if (out->lastwrite() < oldestOutput) oldestOutput = out->lastwrite();
    if (out->lastwrite() == 0) missingOutput = true;
  }
  for (auto& in : inputs) {
    if (in->lastwrite() > oldestOutput) {
      state = PendingCommand::ToBeRun;
      for (auto& o : outputs) {
        o->state = File::ToRebuild;
        for (auto& d : o->dependencies) d->generator->Check();
      }
      return;
    }
    if (in->generator) {
      in->generator->Check();
      if (in->generator->state == PendingCommand::ToBeRun) {
        state = PendingCommand::ToBeRun;
        for (auto& o : outputs) {
          o->state = File::ToRebuild;
          for (auto& d : o->dependencies) d->generator->Check();
        }
        return;
      }
    }
  }
  for (auto& o : outputs) {
    o->state = missingOutput ? File::Error : File::Done;
  }
  state = PendingCommand::Done;
}

void PendingCommand::SetResult(bool success) {
  state = PendingCommand::Done;
  for (auto& o : outputs) {
    o->state = (success ? File::Done : File::Error);
  }
}

bool PendingCommand::CanRun() {
  if (state != PendingCommand::ToBeRun) return false;
  for (auto& in : inputs) {
    if (in->state != File::Source && in->state != File::Done) {
//      printf("Cannot build %s because %s is in %d\n", outputs[0]->path.filename().c_str(), in->path.filename().c_str(), in->state);
      return false;
    }
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const PendingCommand& pc) {
  os << pc.commandToRun << " state=";
  switch(pc.state) {
    case PendingCommand::Unknown: os << "unknown"; break;
    case PendingCommand::ToBeRun: os << "to be run"; break;
    case PendingCommand::Running: os << "running"; break;
    case PendingCommand::Done: os << "done"; break;
  }
  return os;
}

