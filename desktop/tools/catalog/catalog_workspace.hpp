#pragma once
#include "app/workspace.hpp"
namespace bacuri::vms::client
{
class CatalogWorkspace : public Workspace
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int scenario READ scenario WRITE setScenario NOTIFY changed FINAL)
  public:
    explicit CatalogWorkspace(QObject* parent = nullptr) : Workspace({}, parent)
    {
        setScenario(0);
    }
    int scenario() const
    {
        return scenario_;
    }
    bool signedIn() const override
    {
        return true;
    }
    bool sessionActions() const override
    {
        return false;
    }
    void refresh() override
    {
        setScenario(4);
    }
    void loadCommandObjects() override;
    void setScenario(int value);

  private:
    int scenario_{-1};
};
} // namespace bacuri::vms::client
